//! Dependency-free, testable system logic shared by native FLX applications.
use std::{
    fs, io,
    path::{Path, PathBuf},
};

#[derive(Debug, Clone)]
pub struct Entry {
    pub path: PathBuf,
    pub name: String,
    pub is_dir: bool,
    pub is_symlink: bool,
    pub size: u64,
    pub modified: Option<std::time::SystemTime>,
}

pub fn list_dir(path: &Path, show_hidden: bool) -> io::Result<Vec<Entry>> {
    let mut out = Vec::new();
    for item in fs::read_dir(path)? {
        let item = item?;
        let name = item.file_name().to_string_lossy().into_owned();
        if !show_hidden && name.starts_with('.') {
            continue;
        }
        let path = item.path();
        let meta = fs::symlink_metadata(&path)?;
        let is_dir = if meta.file_type().is_symlink() {
            fs::metadata(&path)
                .map(|target| target.is_dir())
                .unwrap_or(false)
        } else {
            meta.is_dir()
        };
        out.push(Entry {
            path,
            name,
            is_dir,
            is_symlink: meta.file_type().is_symlink(),
            size: meta.len(),
            modified: meta.modified().ok(),
        });
    }
    out.sort_by(|a, b| {
        b.is_dir
            .cmp(&a.is_dir)
            .then_with(|| a.name.to_lowercase().cmp(&b.name.to_lowercase()))
    });
    Ok(out)
}

pub fn parent(path: &Path) -> PathBuf {
    path.parent().unwrap_or(Path::new("/")).to_path_buf()
}

#[derive(Debug, Clone, Copy, Default, PartialEq)]
pub struct CpuCounters {
    pub total: u64,
    pub idle: u64,
}
#[derive(Debug, Clone, Copy, PartialEq)]
pub enum Sample {
    First,
    Usage(f64),
    Reset,
}
pub fn cpu_usage(previous: Option<CpuCounters>, current: CpuCounters) -> Sample {
    let Some(prev) = previous else {
        return Sample::First;
    };
    let (Some(total), Some(idle)) = (
        current.total.checked_sub(prev.total),
        current.idle.checked_sub(prev.idle),
    ) else {
        return Sample::Reset;
    };
    if total == 0 || idle > total {
        return Sample::Reset;
    }
    Sample::Usage((100.0 * (total - idle) as f64 / total as f64).clamp(0.0, 100.0))
}

pub fn parse_cpu_stat(line: &str) -> Option<CpuCounters> {
    let mut f = line.split_whitespace();
    let label = f.next()?;
    if label != "cpu"
        && !label
            .strip_prefix("cpu")?
            .bytes()
            .all(|b| b.is_ascii_digit())
    {
        return None;
    }
    let values: Vec<u64> = f.map(str::parse).collect::<Result<_, _>>().ok()?;
    if values.len() < 4 {
        return None;
    }
    let total = values.iter().try_fold(0u64, |a, b| a.checked_add(*b))?;
    Some(CpuCounters {
        total,
        idle: values[3].checked_add(*values.get(4).unwrap_or(&0))?,
    })
}

#[derive(Debug, Clone, Default, PartialEq, Eq)]
pub struct Memory {
    pub total_kib: u64,
    pub available_kib: Option<u64>,
    pub swap_total_kib: Option<u64>,
    pub swap_free_kib: Option<u64>,
}
pub fn parse_meminfo(input: &str) -> Memory {
    let mut m = Memory::default();
    for line in input.lines() {
        let mut p = line.split_whitespace();
        let key = p.next().unwrap_or("").trim_end_matches(':');
        let value = p.next().and_then(|x| x.parse::<u64>().ok());
        match key {
            "MemTotal" => m.total_kib = value.unwrap_or(0),
            "MemAvailable" => m.available_kib = value,
            "SwapTotal" => m.swap_total_kib = value,
            "SwapFree" => m.swap_free_kib = value,
            _ => {}
        }
    }
    m
}

/// Parse `/proc/<pid>/stat`, accounting for spaces and `)` in comm by splitting after its final `)`.
pub fn parse_proc_stat(line: &str) -> Option<ProcStat> {
    let open = line.find('(')?;
    let close = line.rfind(')')?;
    let pid = line[..open].trim().parse().ok()?;
    let name = line[open + 1..close].to_owned();
    let f: Vec<&str> = line[close + 1..].split_whitespace().collect();
    if f.len() <= 21 {
        return None;
    }
    Some(ProcStat {
        pid,
        name,
        state: f[0].chars().next()?,
        ppid: f[1].parse().ok()?,
        ticks: f[11]
            .parse::<u64>()
            .ok()?
            .checked_add(f[12].parse().ok()?)?,
        threads: f[17].parse().ok()?,
        start_ticks: f[19].parse().ok()?,
        virtual_bytes: f[20].parse().ok()?,
        rss_pages: f[21].parse().ok()?,
    })
}
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct ProcStat {
    pub pid: u32,
    pub name: String,
    pub state: char,
    pub ppid: u32,
    pub ticks: u64,
    pub start_ticks: u64,
    pub threads: u32,
    pub virtual_bytes: u64,
    pub rss_pages: i64,
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::time::{SystemTime, UNIX_EPOCH};
    fn fixture() -> PathBuf {
        let stamp = SystemTime::now()
            .duration_since(UNIX_EPOCH)
            .unwrap()
            .as_nanos();
        let p = std::env::temp_dir().join(format!("flx-core-{}-{stamp}", std::process::id()));
        fs::create_dir(&p).unwrap();
        p
    }
    #[test]
    fn cpu_first_delta_and_reset() {
        assert_eq!(
            cpu_usage(None, CpuCounters { total: 10, idle: 8 }),
            Sample::First
        );
        assert_eq!(
            cpu_usage(
                Some(CpuCounters {
                    total: 100,
                    idle: 80
                }),
                CpuCounters {
                    total: 150,
                    idle: 110
                }
            ),
            Sample::Usage(40.0)
        );
        assert_eq!(
            cpu_usage(
                Some(CpuCounters { total: 10, idle: 8 }),
                CpuCounters { total: 3, idle: 2 }
            ),
            Sample::Reset
        );
    }
    #[test]
    fn cpu_parse_aggregates_idle_and_iowait() {
        assert_eq!(
            parse_cpu_stat("cpu 10 2 3 80 5 0 0 0").unwrap(),
            CpuCounters {
                total: 100,
                idle: 85
            }
        );
    }
    #[test]
    fn per_core_counters_parse() {
        assert_eq!(
            parse_cpu_stat("cpu12 3 1 2 10 4"),
            Some(CpuCounters {
                total: 20,
                idle: 14
            })
        );
    }
    #[test]
    fn memory_values_are_optional_not_fabricated() {
        let m = parse_meminfo("MemTotal: 100 kB\nMemAvailable: 25 kB\n");
        assert_eq!(m.total_kib, 100);
        assert_eq!(m.available_kib, Some(25));
        assert_eq!(m.swap_total_kib, None);
    }
    #[test]
    fn proc_name_can_contain_spaces_and_parentheses() {
        let s = parse_proc_stat("123 (worker ) pool) S 1 0 0 0 0 0 0 0 0 0 7 3 0 0 0 0 2 0 0 0 42")
            .unwrap();
        assert_eq!(s.name, "worker ) pool");
        assert_eq!(s.ticks, 10);
        assert_eq!(s.rss_pages, 42);
    }
    #[test]
    fn missing_proc_fields_are_rejected() {
        assert!(parse_proc_stat("123 (x) S 1").is_none());
    }
    #[test]
    fn listing_sorts_hides_dotfiles_and_keeps_unicode_names() {
        let p = fixture();
        fs::create_dir(p.join("Zoo")).unwrap();
        fs::write(p.join("école.txt"), b"ok").unwrap();
        fs::write(p.join(".hidden"), b"secret").unwrap();
        let visible = list_dir(&p, false).unwrap();
        assert_eq!(
            visible.iter().map(|e| e.name.as_str()).collect::<Vec<_>>(),
            ["Zoo", "école.txt"]
        );
        assert_eq!(visible[0].is_dir, true);
        assert_eq!(list_dir(&p, true).unwrap().len(), 3);
        fs::remove_dir_all(p).unwrap();
    }
    #[test]
    fn missing_directory_returns_io_error() {
        assert!(list_dir(Path::new("/definitely-not-a-flx-test-directory"), false).is_err());
    }
    #[cfg(unix)]
    #[test]
    fn directory_symlink_is_navigable_but_remains_identified_as_link() {
        use std::os::unix::fs::symlink;
        let p = fixture();
        fs::create_dir(p.join("target")).unwrap();
        symlink(p.join("target"), p.join("shortcut")).unwrap();
        symlink(p.join("missing"), p.join("broken-link")).unwrap();
        let entries = list_dir(&p, false).unwrap();
        let link = entries.iter().find(|e| e.name == "shortcut").unwrap();
        assert!(link.is_dir);
        assert!(link.is_symlink);
        let broken = entries.iter().find(|e| e.name == "broken-link").unwrap();
        assert!(!broken.is_dir);
        assert!(broken.is_symlink);
        fs::remove_dir_all(p).unwrap();
    }
}
