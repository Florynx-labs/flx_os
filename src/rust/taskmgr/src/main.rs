//! Native FLX Task Manager. The UI calls the existing libflxui ABI; collection runs
//! on one bounded background worker and all presentation state remains on the UI thread.
use flx_core::{
    cpu_usage, parse_cpu_stat, parse_meminfo, parse_proc_stat, CpuCounters, Memory, Sample,
};
use std::{
    collections::{HashMap, VecDeque},
    ffi::{CStr, CString},
    fs,
    path::{Path, PathBuf},
    sync::{
        mpsc::{self, Receiver, SyncSender, TryRecvError},
        Arc, Mutex, OnceLock,
    },
    thread::{self, JoinHandle},
    time::Duration,
};

#[repr(C)]
struct Canvas {
    _p: [u8; 0],
}
#[repr(C)]
struct Ui {
    w: i32,
    h: i32,
    mx: f32,
    my: f32,
    buttons: i32,
    ev: i32,
    btn: i32,
    px: f32,
    py: f32,
    clicks: i32,
    wheel: f32,
    key: i32,
    mods: i32,
    text: [i8; 8],
    focus: i32,
    consumed: i32,
    t: f64,
}
#[repr(C)]
struct AppDef {
    exe: *const i8,
    title: *const i8,
    w: i32,
    h: i32,
    mica: i32,
    init: Option<extern "C" fn(i32, *mut *mut i8)>,
    draw: Option<extern "C" fn(*mut Canvas)>,
    tick: Option<extern "C" fn()>,
    tick_ms: i32,
    close: Option<extern "C" fn() -> i32>,
    extra_fd: i32,
    on_fd: Option<extern "C" fn()>,
}
#[repr(C)]
#[derive(Default, Clone, Copy)]
struct FsStats {
    total: u64,
    available: u64,
    used: u64,
}
#[repr(C)]
#[derive(Default, Clone, Copy)]
struct Scroll {
    pos: f32,
    max: f32,
    drag: i32,
    grab: f32,
}
#[link(name = "w12", kind = "static")]
unsafe extern "C" {
    fn app_main(a: *mut AppDef, argc: i32, argv: *mut *mut i8) -> i32;
    fn app_redraw();
    fn g_rect(c: *mut Canvas, x: f32, y: f32, w: f32, h: f32, col: u32);
    fn g_rrect(c: *mut Canvas, x: f32, y: f32, w: f32, h: f32, rt: f32, rb: f32, col: u32);
    fn g_rrect_line(c: *mut Canvas, x: f32, y: f32, w: f32, h: f32, r: f32, lw: f32, col: u32);
    fn g_line(c: *mut Canvas, x0: f32, y0: f32, x1: f32, y1: f32, lw: f32, col: u32);
    fn g_circle(c: *mut Canvas, x: f32, y: f32, r: f32, col: u32);
    fn g_ring(c: *mut Canvas, x: f32, y: f32, r: f32, lw: f32, col: u32);
    fn g_arc(c: *mut Canvas, x: f32, y: f32, r: f32, a0: f32, a1: f32, lw: f32, col: u32);
    fn g_clip(c: *mut Canvas, x0: f32, y0: f32, x1: f32, y1: f32);
    fn g_getclip(c: *mut Canvas, s: *mut i32);
    fn g_setclip(c: *mut Canvas, s: *const i32);
    fn text(
        c: *mut Canvas,
        font: i32,
        px: f32,
        track: f32,
        x: f32,
        base: f32,
        col: u32,
        s: *const i8,
    ) -> f32;
    #[link_name = "ui_button"]
    fn ffi_ui_button(x: f32, y: f32, w: f32, h: f32, label: *const i8, kind: i32) -> bool;
    fn ui_click(x: f32, y: f32, w: f32, h: f32) -> bool;
    fn ui_hover(x: f32, y: f32, w: f32, h: f32) -> bool;
    fn ui_downin(x: f32, y: f32, w: f32, h: f32) -> bool;
    #[link_name = "ui_field"]
    fn ffi_ui_field(
        id: i32,
        buf: *mut i8,
        cap: i32,
        x: f32,
        y: f32,
        w: f32,
        h: f32,
        ph: *const i8,
    ) -> i32;
    fn ui_scroll(s: *mut Scroll, x: f32, y: f32, w: f32, h: f32, content_h: f32);
    fn icon_app(c: *mut Canvas, app: i32, x: f32, y: f32, size: f32);
    fn icon_glyph(c: *mut Canvas, glyph: i32, x: f32, y: f32, size: f32, col: u32);
    fn app_launch(exe: *const i8, arg: *const i8);
    fn app_by_exe(exe: *const i8) -> i32;
    fn flx_fs_stats(path: *const i8, out: *mut FsStats) -> i32;
    fn kill(pid: i32, sig: i32) -> i32;
    fn getuid() -> u32;
    fn MARK(fmt: *const i8, ...);
    static mut U: Ui;
}

const BG: u32 = 0xff020b10;
const WIN: u32 = 0xff031116;
const PANEL: u32 = 0xff04151b;
const CARD: u32 = 0xff061a20;
const CYAN: u32 = 0xff29e6e6;
const CYAN2: u32 = 0xff14b8b8;
const GOLD: u32 = 0xffffb72c;
const TXT: u32 = 0xffeaf4f7;
const SUB: u32 = 0xffa7c2cc;
const MUTED: u32 = 0xff668894;
const LINE: u32 = 0xff0b2b34;
const GREEN: u32 = 0xff00d6a3;
const RED: u32 = 0xffff4d55;
const WARNING: u32 = 0xffffb72c;
const SIGTERM: i32 = 15;
const SIGKILL: i32 = 9;
const K_UP: i32 = 0x110002;
const K_DOWN: i32 = 0x110003;
const K_ESC: i32 = 27;

#[derive(Clone, Debug)]
struct Process {
    pid: u32,
    ppid: u32,
    name: String,
    cmd: String,
    exe: Option<PathBuf>,
    state: char,
    uid: u32,
    rss_kib: u64,
    vsize_kib: u64,
    threads: u32,
    start: u64,
    cpu: Option<f64>,
    read_bps: Option<u64>,
    write_bps: Option<u64>,
}
#[derive(Clone, Default)]
struct Snapshot {
    processes: Vec<Process>,
    cpu: Option<f64>,
    cores: Vec<Option<f64>>,
    memory: Memory,
    net_rx_bps: Option<u64>,
    net_tx_bps: Option<u64>,
    disk_read_bps: Option<u64>,
    disk_write_bps: Option<u64>,
    filesystem: Option<FsStats>,
    sensors: Vec<(String, f64)>,
    cpu_name: String,
    cpu_mhz: Option<f64>,
    kernel: String,
    os_name: String,
    uptime_secs: Option<u64>,
}
#[derive(Clone, Copy, PartialEq)]
enum Page {
    Processes,
    Performance,
    Applications,
    Startup,
    Users,
    Details,
    Services,
}
impl Page {
    const ALL: [Page; 7] = [
        Page::Processes,
        Page::Performance,
        Page::Applications,
        Page::Startup,
        Page::Users,
        Page::Details,
        Page::Services,
    ];
    fn label(self) -> &'static str {
        match self {
            Self::Processes => "Processus",
            Self::Performance => "Performance",
            Self::Applications => "Applications",
            Self::Startup => "Démarrage",
            Self::Users => "Utilisateurs",
            Self::Details => "Détails",
            Self::Services => "Services",
        }
    }
    fn glyph(self) -> i32 {
        match self {
            Self::Processes => 27,
            Self::Performance => 39,
            Self::Applications => 42,
            Self::Startup => 37,
            Self::Users => 24,
            Self::Details => 39,
            Self::Services => 23,
        }
    }
}
#[derive(Clone, Copy, PartialEq)]
enum Sort {
    Name,
    Pid,
    Cpu,
    Memory,
    State,
}
#[derive(Clone, Copy, PartialEq)]
enum Filter {
    All,
    Current,
    Running,
    Sleeping,
    System,
}
#[derive(Clone, Copy)]
enum Pending {
    Terminate,
    Force,
}
#[derive(Default)]
struct History {
    cpu: VecDeque<f64>,
    memory: VecDeque<f64>,
    disk: VecDeque<f64>,
    network: VecDeque<f64>,
}
struct AppState {
    page: Page,
    snapshot: Snapshot,
    selected: Option<(u32, u64)>,
    query: [i8; 160],
    filter: Filter,
    sort: Sort,
    ascending: bool,
    scroll: Scroll,
    history: History,
    status: String,
    pending: Option<Pending>,
    sampling: bool,
}
impl Default for AppState {
    fn default() -> Self {
        Self {
            page: Page::Processes,
            snapshot: Snapshot::default(),
            selected: None,
            query: [0; 160],
            filter: Filter::All,
            sort: Sort::Memory,
            ascending: false,
            scroll: Scroll::default(),
            history: History::default(),
            status: "Collecte des métriques système…".into(),
            pending: None,
            sampling: false,
        }
    }
}
struct Worker {
    stop: Arc<std::sync::atomic::AtomicBool>,
    join: Option<JoinHandle<()>>,
    rx: Receiver<Snapshot>,
}
static STATE: OnceLock<Mutex<AppState>> = OnceLock::new();
static WORKER: OnceLock<Mutex<Option<Worker>>> = OnceLock::new();
fn state() -> &'static Mutex<AppState> {
    STATE.get_or_init(|| Mutex::new(AppState::default()))
}
fn worker() -> &'static Mutex<Option<Worker>> {
    WORKER.get_or_init(|| Mutex::new(None))
}
fn with_state<R>(f: impl FnOnce(&mut AppState) -> R) -> R {
    let mut g = state().lock().unwrap_or_else(|e| e.into_inner());
    f(&mut g)
}
fn cstr(s: &str) -> CString {
    CString::new(s.replace('\0', "�")).unwrap_or_default()
}
fn draw_text(c: *mut Canvas, font: i32, size: f32, x: f32, y: f32, color: u32, s: &str) {
    let t = cstr(s);
    unsafe { text(c, font, size, 0.0, x, y, color, t.as_ptr()) };
}
unsafe fn ui_button(x: f32, y: f32, w: f32, h: f32, label: &str, kind: i32) -> bool {
    let v = cstr(label);
    ffi_ui_button(x, y, w, h, v.as_ptr(), kind)
}
unsafe fn ui_field(
    id: i32,
    buf: *mut i8,
    cap: i32,
    x: f32,
    y: f32,
    w: f32,
    h: f32,
    placeholder: &str,
) -> i32 {
    let v = cstr(placeholder);
    ffi_ui_field(id, buf, cap, x, y, w, h, v.as_ptr())
}
fn box_panel(c: *mut Canvas, x: f32, y: f32, w: f32, h: f32) {
    unsafe {
        g_rrect(c, x, y, w, h, 9.0, 9.0, PANEL);
        g_rrect_line(c, x, y, w, h, 9.0, 1.0, LINE)
    }
}
fn fmt_bytes(n: u64) -> String {
    const U: [&str; 5] = ["B", "KB", "MB", "GB", "TB"];
    let mut f = n as f64;
    let mut i = 0;
    while f >= 1024.0 && i < 4 {
        f /= 1024.0;
        i += 1
    }
    if i == 0 {
        format!("{f:.0} {}", U[i])
    } else if f < 10.0 {
        format!("{f:.1} {}", U[i])
    } else {
        format!("{f:.0} {}", U[i])
    }
}
fn fmt_rate(v: Option<u64>) -> String {
    v.map(|n| format!("{}/s", fmt_bytes(n)))
        .unwrap_or_else(|| "—".into())
}
fn pct(v: Option<f64>) -> String {
    v.map(|x| format!("{x:.0}%")).unwrap_or_else(|| "—".into())
}
fn selected_process(s: &AppState) -> Option<&Process> {
    let (pid, start) = s.selected?;
    s.snapshot
        .processes
        .iter()
        .find(|p| p.pid == pid && p.start == start)
}

#[derive(Default)]
struct Sampler {
    cpu: Option<CpuCounters>,
    core: HashMap<String, CpuCounters>,
    proc: HashMap<(u32, u64), (u64, u64, u64)>,
    net: Option<(u64, u64)>,
    disk: Option<(u64, u64)>,
    last: Option<std::time::Instant>,
}
fn read_counter_file(path: &str) -> Option<String> {
    fs::read_to_string(path).ok()
}
fn network_totals() -> Option<(u64, u64)> {
    let s = read_counter_file("/proc/net/dev")?;
    let mut rx = 0u64;
    let mut tx = 0u64;
    let mut seen = false;
    for l in s.lines().skip(2) {
        let (name, vals) = l.split_once(':')?;
        if name.trim() == "lo" {
            continue;
        }
        let v: Vec<u64> = vals
            .split_whitespace()
            .filter_map(|x| x.parse().ok())
            .collect();
        if v.len() >= 9 {
            rx = rx.saturating_add(v[0]);
            tx = tx.saturating_add(v[8]);
            seen = true
        }
    }
    seen.then_some((rx, tx))
}
fn disk_totals() -> Option<(u64, u64)> {
    let s = read_counter_file("/proc/diskstats")?;
    let mut rd = 0u64;
    let mut wr = 0u64;
    let mut seen = false;
    for l in s.lines() {
        let f: Vec<&str> = l.split_whitespace().collect();
        if f.len() < 10 {
            continue;
        }
        let name = f[2];
        if name.starts_with("loop") || name.starts_with("ram") || name.starts_with("zram") {
            continue;
        }
        let dev = format!("/sys/dev/block/{}:{}/partition", f[0], f[1]);
        if Path::new(&dev).exists() {
            continue;
        }
        let (Ok(r), Ok(w)) = (f[5].parse::<u64>(), f[9].parse::<u64>()) else {
            continue;
        };
        rd = rd.saturating_add(r.saturating_mul(512));
        wr = wr.saturating_add(w.saturating_mul(512));
        seen = true
    }
    seen.then_some((rd, wr))
}
fn read_uid(pid: u32) -> u32 {
    fs::read_to_string(format!("/proc/{pid}/status"))
        .ok()
        .and_then(|s| {
            s.lines()
                .find(|l| l.starts_with("Uid:"))
                .and_then(|l| l.split_whitespace().nth(1)?.parse().ok())
        })
        .unwrap_or(u32::MAX)
}
fn read_io(pid: u32) -> (u64, u64) {
    let Ok(s) = fs::read_to_string(format!("/proc/{pid}/io")) else {
        return (0, 0);
    };
    let mut r = 0;
    let mut w = 0;
    for l in s.lines() {
        if let Some(v) = l.strip_prefix("read_bytes:") {
            r = v.trim().parse().unwrap_or(0)
        } else if let Some(v) = l.strip_prefix("write_bytes:") {
            w = v.trim().parse().unwrap_or(0)
        }
    }
    (r, w)
}
fn read_sensors() -> Vec<(String, f64)> {
    let mut out = Vec::new();
    if let Ok(zones) = fs::read_dir("/sys/class/thermal") {
        for z in zones.flatten() {
            let n = z.file_name().to_string_lossy().to_string();
            if !n.starts_with("thermal_zone") {
                continue;
            }
            let label = fs::read_to_string(z.path().join("type"))
                .unwrap_or_else(|_| n.clone())
                .trim()
                .to_owned();
            if let Ok(t) = fs::read_to_string(z.path().join("temp")) {
                if let Ok(v) = t.trim().parse::<f64>() {
                    let c = if v.abs() > 1000.0 { v / 1000.0 } else { v };
                    if (-20.0..=150.0).contains(&c) {
                        out.push((label, c));
                    }
                }
            }
        }
    }
    if let Ok(devs) = fs::read_dir("/sys/class/hwmon") {
        for d in devs.flatten() {
            let chip = fs::read_to_string(d.path().join("name"))
                .unwrap_or_else(|_| "hwmon".into())
                .trim()
                .to_owned();
            let Ok(files) = fs::read_dir(d.path()) else {
                continue;
            };
            for f in files.flatten() {
                let n = f.file_name().to_string_lossy().to_string();
                if !n.starts_with("temp") || !n.ends_with("_input") {
                    continue;
                }
                let stem = n.trim_end_matches("_input");
                let label = fs::read_to_string(d.path().join(format!("{stem}_label")))
                    .unwrap_or_else(|_| stem.into())
                    .trim()
                    .to_owned();
                if let Ok(t) = fs::read_to_string(f.path()) {
                    if let Ok(v) = t.trim().parse::<f64>() {
                        let c = if v.abs() > 1000.0 { v / 1000.0 } else { v };
                        if (-20.0..=150.0).contains(&c)
                            && !out.iter().any(|(l, x)| *l == label && (*x - c).abs() < 0.5)
                        {
                            out.push((format!("{chip} {label}"), c));
                        }
                    }
                }
            }
        }
    }
    out.sort_by(|a, b| a.0.cmp(&b.0));
    out.dedup_by(|a, b| a.0 == b.0);
    out
}
fn collect(samp: &mut Sampler) -> Snapshot {
    let now = std::time::Instant::now();
    let elapsed = samp
        .last
        .map(|last| now.duration_since(last).as_secs_f64())
        .unwrap_or(1.0)
        .max(0.001);
    samp.last = Some(now);
    let old_cpu = samp.cpu;
    let mut out = Snapshot::default();
    let mut total = None;
    let mut cores = HashMap::new();
    if let Some(stat) = read_counter_file("/proc/stat") {
        for l in stat.lines() {
            if let Some((label, _)) = l.split_once(' ') {
                if let Some(c) = parse_cpu_stat(l) {
                    if label == "cpu" {
                        total = Some(c)
                    } else if label.starts_with("cpu") {
                        cores.insert(label.to_owned(), c);
                    }
                }
            }
        }
    }
    if let Some(cur) = total {
        out.cpu = match cpu_usage(samp.cpu, cur) {
            Sample::Usage(v) => Some(v),
            _ => None,
        };
        samp.cpu = Some(cur)
    }
    for (name, cur) in &cores {
        out.cores.push(
            samp.core
                .get(name)
                .copied()
                .and_then(|p| match cpu_usage(Some(p), *cur) {
                    Sample::Usage(v) => Some(v),
                    _ => None,
                }),
        );
    }
    samp.core = cores;
    if let Some(m) = read_counter_file("/proc/meminfo") {
        out.memory = parse_meminfo(&m)
    }
    let ncpu = samp.core.len().max(1) as f64;
    let mut next = HashMap::new();
    let mut processes = Vec::new();
    if let Ok(entries) = fs::read_dir("/proc") {
        for ent in entries.flatten() {
            let Some(pid) = ent.file_name().to_str().and_then(|x| x.parse::<u32>().ok()) else {
                continue;
            };
            let Ok(st) = fs::read_to_string(ent.path().join("stat")) else {
                continue;
            };
            let Some(p) = parse_proc_stat(&st) else {
                continue;
            };
            let key = (pid, p.start_ticks);
            let (rbytes, wbytes) = read_io(pid);
            let old = samp.proc.get(&key).copied();
            let (cpu, rbps, wbps) =
                if let (Some(prev), Some(prev_total), Some(cur_total)) = (old, old_cpu, total) {
                    let dt = p.ticks.saturating_sub(prev.0);
                    let dtotal = cur_total.total.saturating_sub(prev_total.total);
                    (
                        if dtotal > 0 {
                            Some(dt as f64 * ncpu * 100.0 / dtotal as f64)
                        } else {
                            None
                        },
                        Some(rbytes.saturating_sub(prev.1) / (elapsed as u64).max(1)),
                        Some(wbytes.saturating_sub(prev.2) / (elapsed as u64).max(1)),
                    )
                } else {
                    (None, None, None)
                };
            let cmd = fs::read(ent.path().join("cmdline"))
                .ok()
                .map(|b| {
                    String::from_utf8_lossy(&b)
                        .replace('\0', " ")
                        .trim()
                        .to_owned()
                })
                .filter(|s| !s.is_empty())
                .unwrap_or_else(|| p.name.clone());
            let exe = fs::read_link(ent.path().join("exe")).ok();
            let rss = p.rss_pages.max(0) as u64 * 4;
            next.insert(key, (p.ticks, rbytes, wbytes));
            processes.push(Process {
                pid,
                ppid: p.ppid,
                name: p.name,
                cmd,
                exe,
                state: p.state,
                uid: read_uid(pid),
                rss_kib: rss,
                vsize_kib: p.virtual_bytes / 1024,
                threads: p.threads,
                start: p.start_ticks,
                cpu,
                read_bps: rbps,
                write_bps: wbps,
            });
        }
    }
    samp.proc = next;
    processes.sort_by_key(|p| p.pid);
    out.processes = processes;
    if let (Some(cur), Some(prev)) = (network_totals(), samp.net) {
        out.net_rx_bps = Some(cur.0.saturating_sub(prev.0) / (elapsed as u64).max(1));
        out.net_tx_bps = Some(cur.1.saturating_sub(prev.1) / (elapsed as u64).max(1));
    }
    samp.net = network_totals();
    if let (Some(cur), Some(prev)) = (disk_totals(), samp.disk) {
        out.disk_read_bps = Some(cur.0.saturating_sub(prev.0) / (elapsed as u64).max(1));
        out.disk_write_bps = Some(cur.1.saturating_sub(prev.1) / (elapsed as u64).max(1));
    }
    samp.disk = disk_totals();
    let mut fsstats = FsStats::default();
    if unsafe { flx_fs_stats(b"/\0".as_ptr() as _, &mut fsstats) } == 0 {
        out.filesystem = Some(fsstats)
    }
    out.sensors = read_sensors();
    if let Some(info) = read_counter_file("/proc/cpuinfo") {
        out.cpu_name = info
            .lines()
            .find(|l| l.starts_with("model name") || l.starts_with("Hardware"))
            .and_then(|l| l.split_once(':'))
            .map(|(_, v)| v.trim().to_owned())
            .unwrap_or_default();
        out.cpu_mhz = info
            .lines()
            .find(|l| l.starts_with("cpu MHz"))
            .and_then(|l| l.split_once(':')?.1.trim().parse().ok());
    }
    out.kernel = read_counter_file("/proc/sys/kernel/osrelease")
        .unwrap_or_else(|| "Non disponible".into())
        .trim()
        .into();
    out.os_name = read_counter_file("/etc/os-release")
        .and_then(|s| {
            s.lines().find(|l| l.starts_with("PRETTY_NAME=")).map(|l| {
                l.trim_start_matches("PRETTY_NAME=")
                    .trim_matches('"')
                    .to_owned()
            })
        })
        .unwrap_or_else(|| "FLX OS".into());
    out.uptime_secs = read_counter_file("/proc/uptime").and_then(|s| {
        s.split_whitespace()
            .next()?
            .parse::<f64>()
            .ok()
            .map(|v| v as u64)
    });
    out
}
fn start_worker() {
    let mut slot = worker().lock().unwrap_or_else(|e| e.into_inner());
    if slot.is_some() {
        return;
    }
    let (tx, rx): (SyncSender<Snapshot>, Receiver<Snapshot>) = mpsc::sync_channel(1);
    let stop = Arc::new(std::sync::atomic::AtomicBool::new(false));
    let flag = stop.clone();
    let join = thread::Builder::new()
        .name("flx-taskmgr-sampler".into())
        .spawn(move || {
            let mut samp = Sampler::default();
            while !flag.load(std::sync::atomic::Ordering::Relaxed) {
                let snapshot = collect(&mut samp);
                match tx.try_send(snapshot) {
                    Ok(()) | Err(mpsc::TrySendError::Full(_)) => {}
                    Err(mpsc::TrySendError::Disconnected(_)) => break,
                }
                for _ in 0..10 {
                    if flag.load(std::sync::atomic::Ordering::Relaxed) {
                        break;
                    }
                    thread::sleep(Duration::from_millis(100));
                }
            }
        });
    if let Ok(join) = join {
        *slot = Some(Worker {
            stop,
            join: Some(join),
            rx,
        })
    }
}
fn stop_worker() {
    if let Some(mut w) = worker().lock().unwrap_or_else(|e| e.into_inner()).take() {
        w.stop.store(true, std::sync::atomic::Ordering::Relaxed);
        if let Some(j) = w.join.take() {
            let _ = j.join();
        }
    }
}
extern "C" fn tick() {
    let mut fresh = None;
    {
        let mut guard = worker().lock().unwrap_or_else(|e| e.into_inner());
        if let Some(w) = guard.as_mut() {
            loop {
                match w.rx.try_recv() {
                    Ok(s) => fresh = Some(s),
                    Err(TryRecvError::Empty | TryRecvError::Disconnected) => break,
                }
            }
        }
    }
    if let Some(snapshot) = fresh {
        with_state(|s| {
            if let Some((pid, start)) = s.selected {
                if !snapshot
                    .processes
                    .iter()
                    .any(|p| p.pid == pid && p.start == start)
                {
                    s.selected = None
                }
            }
            let mem = if snapshot.memory.total_kib > 0 && snapshot.memory.available_kib.is_some() {
                Some(
                    100.0
                        * (snapshot
                            .memory
                            .total_kib
                            .saturating_sub(snapshot.memory.available_kib.unwrap_or(0)))
                            as f64
                        / snapshot.memory.total_kib as f64,
                )
            } else {
                None
            };
            for (q, v) in [
                (&mut s.history.cpu, snapshot.cpu),
                (&mut s.history.memory, mem),
                (
                    &mut s.history.disk,
                    snapshot.disk_read_bps.map(|v| v as f64),
                ),
                (
                    &mut s.history.network,
                    snapshot.net_rx_bps.map(|v| v as f64),
                ),
            ] {
                if let Some(x) = v {
                    q.push_back(x);
                    if q.len() > 90 {
                        q.pop_front();
                    }
                }
            }
            s.snapshot = snapshot;
            s.sampling = true
        });
    }
    unsafe { app_redraw() }
}
extern "C" fn close() -> i32 {
    stop_worker();
    1
}

fn metric_card(
    c: *mut Canvas,
    x: f32,
    y: f32,
    w: f32,
    h: f32,
    title: &str,
    value: &str,
    detail: &str,
    color: u32,
    percent: Option<f64>,
    history: &VecDeque<f64>,
) {
    box_panel(c, x, y, w, h);
    draw_text(c, 3, 14.0, x + 14.0, y + 23.0, color, title);
    let cx = x + 35.0;
    let cy = y + 71.0;
    unsafe {
        g_ring(c, cx, cy, 21.0, 4.0, 0xff12333a);
        if let Some(p) = percent {
            let end = -std::f32::consts::FRAC_PI_2
                + std::f32::consts::TAU * p.clamp(0.0, 100.0) as f32 / 100.0;
            g_arc(
                c,
                cx,
                cy,
                21.0,
                -std::f32::consts::FRAC_PI_2,
                end,
                4.0,
                color,
            )
        }
    }
    draw_text(
        c,
        3,
        12.0,
        cx - 14.0,
        cy + 4.0,
        color,
        &percent
            .map(|p| format!("{p:.0}%"))
            .unwrap_or_else(|| "—".into()),
    );
    draw_text(c, 3, 13.0, x + 65.0, y + 61.0, TXT, value);
    draw_text(c, 2, 10.0, x + 65.0, y + 81.0, MUTED, detail);
    let gx = x + 12.0;
    let gy = y + h - 25.0;
    let gw = w - 24.0;
    let gh = 13.0;
    unsafe { g_rect(c, gx, gy, gw, 1.0, LINE) }
    if history.len() > 1 {
        let max = history.iter().copied().fold(0.01f64, f64::max);
        let vals: Vec<f64> = history.iter().copied().collect();
        for i in 1..vals.len() {
            let x0 = gx + gw * (i - 1) as f32 / (vals.len() - 1) as f32;
            let x1 = gx + gw * i as f32 / (vals.len() - 1) as f32;
            let y0 = gy + gh - (vals[i - 1] / max) as f32 * gh;
            let y1 = gy + gh - (vals[i] / max) as f32 * gh;
            unsafe { g_line(c, x0, y0, x1, y1, 1.5, color) }
        }
    }
}
fn chart(
    c: *mut Canvas,
    x: f32,
    y: f32,
    w: f32,
    h: f32,
    title: &str,
    series: &[(&VecDeque<f64>, u32)],
    percent: bool,
) {
    box_panel(c, x, y, w, h);
    draw_text(c, 3, 13.0, x + 12.0, y + 20.0, CYAN, title);
    let gx = x + 12.0;
    let gy = y + 32.0;
    let gw = w - 24.0;
    let gh = h - 46.0;
    for i in 0..=3 {
        let yy = gy + gh * i as f32 / 3.0;
        unsafe { g_line(c, gx, yy, gx + gw, yy, 1.0, LINE) }
        if percent {
            draw_text(
                c,
                2,
                8.0,
                gx + gw - 28.0,
                yy - 2.0,
                MUTED,
                &format!("{}%", 100 - i * 33),
            )
        }
    }
    for (hist, col) in series {
        let vals: Vec<f64> = hist.iter().copied().collect();
        if vals.len() < 2 {
            continue;
        }
        let max = if percent {
            100.0
        } else {
            vals.iter().copied().fold(1.0, f64::max)
        };
        for i in 1..vals.len() {
            let x0 = gx + gw * (i - 1) as f32 / (vals.len() - 1) as f32;
            let x1 = gx + gw * i as f32 / (vals.len() - 1) as f32;
            let y0 = gy + gh - (vals[i - 1] / max).clamp(0.0, 1.0) as f32 * gh;
            let y1 = gy + gh - (vals[i] / max).clamp(0.0, 1.0) as f32 * gh;
            unsafe { g_line(c, x0, y0, x1, y1, 1.7, *col) }
        }
    }
}
fn draw_header(c: *mut Canvas, w: f32) {
    unsafe {
        g_rect(c, 0.0, 0.0, w, 68.0, WIN);
        g_line(c, 0.0, 67.0, w, 67.0, 1.0, LINE);
        icon_app(c, 6, 27.0, 13.0, 34.0)
    }
    draw_text(c, 3, 22.0, 70.0, 31.0, TXT, "Gestionnaire de tâches");
    draw_text(
        c,
        2,
        12.0,
        70.0,
        51.0,
        SUB,
        "Surveillez et gérez vos processus",
    );
    draw_text(c, 2, 10.0, w - 34.0, 48.0, MUTED, "FLX OS · Rust")
}
fn draw_sidebar(c: *mut Canvas, s: &mut AppState, w: f32, h: f32) {
    unsafe {
        g_rect(c, 0.0, 68.0, 174.0, h - 68.0, 0xff030e13);
        g_line(c, 173.0, 68.0, 173.0, h, 1.0, LINE)
    }
    for (i, p) in Page::ALL.iter().enumerate() {
        let y = 86.0 + i as f32 * 48.0;
        let active = s.page == *p;
        if active {
            unsafe {
                g_rrect(c, 10.0, y, 152.0, 40.0, 7.0, 7.0, 0xff172321);
                g_rrect_line(c, 10.0, y, 152.0, 40.0, 7.0, 1.0, 0xff57451f);
                g_rrect(c, 10.0, y + 7.0, 3.0, 26.0, 2.0, 2.0, GOLD)
            }
        }
        if unsafe { ui_hover(10.0, y, 152.0, 40.0) } && !active {
            unsafe { g_rrect(c, 10.0, y, 152.0, 40.0, 7.0, 7.0, 0xff0a1b20) }
        }
        unsafe {
            icon_glyph(
                c,
                p.glyph(),
                30.0,
                y + 20.0,
                16.0,
                if active { GOLD } else { SUB },
            )
        }
        draw_text(
            c,
            2,
            13.0,
            50.0,
            y + 25.0,
            if active { TXT } else { SUB },
            p.label(),
        );
        if unsafe { ui_click(10.0, y, 152.0, 40.0) } {
            s.page = *p;
            s.scroll.pos = 0.0
        }
    }
    unsafe {
        g_line(c, 10.0, h - 64.0, 164.0, h - 64.0, 1.0, LINE);
        icon_app(c, 6, 28.0, h - 37.0, 28.0)
    }
    draw_text(c, 3, 12.0, 51.0, h - 36.0, TXT, "FLX OS");
    draw_text(c, 2, 10.0, 51.0, h - 20.0, MUTED, "Surveillance FLX v1.0");
    let _ = w;
}
fn draw_right(c: *mut Canvas, s: &mut AppState, x: f32, y: f32, w: f32, h: f32) {
    let snap = &s.snapshot;
    let top_h = (h * 0.39).max(190.0);
    box_panel(c, x, y, w, top_h);
    unsafe { icon_glyph(c, 34, x + 16.0, y + 19.0, 14.0, CYAN) }
    draw_text(c, 3, 13.0, x + 30.0, y + 23.0, TXT, "Informations système");
    let mut yy = y + 49.0;
    draw_text(c, 2, 11.0, x + 12.0, yy, SUB, "Système");
    yy += 17.0;
    draw_text(c, 2, 11.0, x + 12.0, yy, CYAN2, &snap.os_name);
    yy += 15.0;
    draw_text(
        c,
        2,
        10.5,
        x + 12.0,
        yy,
        MUTED,
        &format!("Linux {} · {}", snap.kernel, std::env::consts::ARCH),
    );
    yy += 24.0;
    draw_text(c, 2, 11.0, x + 12.0, yy, SUB, "Processeur");
    yy += 16.0;
    draw_text(c, 2, 9.0, x + 12.0, yy, TXT, &truncate(&snap.cpu_name, 24));
    yy += 14.0;
    draw_text(
        c,
        2,
        10.5,
        x + 12.0,
        yy,
        MUTED,
        &format!(
            "{} cœurs · {}",
            snap.cores.len(),
            snap.cpu_mhz
                .map(|v| format!("{v:.0} MHz"))
                .unwrap_or_else(|| "fréquence indisponible".into())
        ),
    );
    yy += 23.0;
    let mem_used = snap
        .memory
        .available_kib
        .map(|a| snap.memory.total_kib.saturating_sub(a));
    draw_text(c, 2, 11.0, x + 12.0, yy, SUB, "Mémoire");
    yy += 16.0;
    draw_text(
        c,
        2,
        10.5,
        x + 12.0,
        yy,
        TXT,
        &format!(
            "{} / {}",
            mem_used
                .map(|v| fmt_bytes(v * 1024))
                .unwrap_or_else(|| "Non disponible".into()),
            fmt_bytes(snap.memory.total_kib * 1024)
        ),
    );
    yy += 24.0;
    draw_text(c, 2, 11.0, x + 12.0, yy, SUB, "Stockage · /");
    yy += 16.0;
    let fs = snap.filesystem;
    draw_text(
        c,
        2,
        10.5,
        x + 12.0,
        yy,
        TXT,
        &fs.map(|f| format!("{} / {}", fmt_bytes(f.used), fmt_bytes(f.total)))
            .unwrap_or_else(|| "Non disponible".into()),
    );
    let action_y = y + top_h + 12.0;
    let action_h = (h * 0.30).max(142.0);
    box_panel(c, x, action_y, w, action_h);
    unsafe { icon_glyph(c, 46, x + 16.0, action_y + 19.0, 14.0, CYAN) }
    draw_text(
        c,
        3,
        12.0,
        x + 30.0,
        action_y + 23.0,
        TXT,
        "Actions rapides",
    );
    let selected = selected_process(s).cloned();
    let enabled = selected.as_ref().is_some_and(|p| p.pid != 1);
    let b_y = action_y + 36.0;
    if unsafe {
        ui_button(
            x + 10.0,
            b_y,
            w - 20.0,
            29.0,
            "Fin de tâche",
            if enabled { 1 } else { 0 },
        )
    } {
        if enabled {
            s.pending = Some(Pending::Terminate)
        } else {
            s.status = "Sélectionnez un processus terminable.".into()
        }
    }
    if unsafe {
        ui_button(
            x + 10.0,
            b_y + 32.0,
            w - 20.0,
            29.0,
            "Forcer l'arrêt…",
            if enabled { 0 } else { 2 },
        )
    } {
        if enabled {
            s.pending = Some(Pending::Force)
        } else {
            s.status = "Aucun processus sélectionné.".into()
        }
    }
    if unsafe {
        ui_button(
            x + 10.0,
            b_y + 64.0,
            w - 20.0,
            29.0,
            "Redémarrer le processus",
            2,
        )
    } {
        s.status = "Redémarrage désactivé : relancer un processus arbitraire n'est pas sûr.".into();
    }
    if unsafe {
        ui_button(
            x + 10.0,
            b_y + 96.0,
            w - 20.0,
            29.0,
            "Ouvrir l'emplacement",
            if selected.as_ref().and_then(|p| p.exe.as_ref()).is_some() {
                0
            } else {
                2
            },
        )
    } {
        if let Some(path) = selected.as_ref().and_then(|p| p.exe.as_ref()) {
            let arg = path.parent().unwrap_or(Path::new("/")).to_string_lossy();
            if !arg.contains('\t') {
                let a = cstr(&arg);
                unsafe { app_launch(b"explorer\0".as_ptr() as _, a.as_ptr()) }
            } else {
                s.status = "Chemin non accepté par le lanceur.".into()
            }
        } else {
            s.status = "Chemin exécutable indisponible.".into()
        }
    }
    if unsafe {
        ui_button(
            x + 10.0,
            b_y + 128.0,
            w - 20.0,
            29.0,
            "Rechercher en ligne",
            if selected.is_some() { 0 } else { 2 },
        )
    } {
        if let Some(p) = selected.as_ref() {
            let url = format!("https://duckduckgo.com/?q={}", url_encode(&p.name));
            let a = cstr(&url);
            unsafe { app_launch(b"browser\0".as_ptr() as _, a.as_ptr()) }
        }
    }
    let sensor_y = action_y + action_h + 12.0;
    let sensor_h = (y + h - sensor_y).max(85.0);
    box_panel(c, x, sensor_y, w, sensor_h);
    unsafe { icon_glyph(c, 39, x + 16.0, sensor_y + 18.0, 14.0, CYAN) }
    draw_text(c, 3, 12.0, x + 30.0, sensor_y + 22.0, TXT, "Températures");
    if snap.sensors.is_empty() {
        draw_text(
            c,
            2,
            10.5,
            x + 12.0,
            sensor_y + 48.0,
            MUTED,
            "Capteur non disponible",
        )
    } else {
        for (i, (name, temp)) in snap.sensors.iter().take(3).enumerate() {
            let sy = sensor_y + 47.0 + i as f32 * 21.0;
            draw_text(c, 2, 9.0, x + 12.0, sy, SUB, &truncate(name, 18));
            draw_text(c, 3, 10.0, x + w - 48.0, sy, TXT, &format!("{temp:.0}°C"))
        }
    }
}
fn truncate(s: &str, n: usize) -> String {
    let mut c = s.chars();
    let t: String = c.by_ref().take(n).collect();
    if c.next().is_some() {
        format!("{t}…")
    } else {
        t
    }
}
fn url_encode(s: &str) -> String {
    let mut o = String::new();
    for b in s.bytes() {
        if b.is_ascii_alphanumeric() || b"-_.~".contains(&b) {
            o.push(b as char)
        } else {
            o.push_str(&format!("%{b:02X}"))
        }
    }
    o
}
fn visible_processes(s: &AppState, page: Page) -> Vec<Process> {
    let q = unsafe {
        CStr::from_ptr(s.query.as_ptr())
            .to_string_lossy()
            .to_lowercase()
    };
    let me = unsafe { getuid() };
    let mut p: Vec<Process> = s
        .snapshot
        .processes
        .iter()
        .filter(|p| match page {
            Page::Applications => p
                .exe
                .as_ref()
                .and_then(|x| x.file_name())
                .and_then(|x| x.to_str())
                .is_some_and(|x| {
                    let c = cstr(x);
                    unsafe { app_by_exe(c.as_ptr()) >= 0 }
                }),
            Page::Services => matches!(
                p.name.as_str(),
                "init" | "flx-netd" | "flx-compositor" | "flxsh"
            ),
            _ => true,
        })
        .filter(|p| match s.filter {
            Filter::All => true,
            Filter::Current => p.uid == me,
            Filter::Running => p.state == 'R',
            Filter::Sleeping => p.state == 'S' || p.state == 'D',
            Filter::System => matches!(
                p.name.as_str(),
                "init" | "flx-netd" | "flx-compositor" | "flxsh"
            ),
        })
        .filter(|p| {
            q.is_empty()
                || p.name.to_lowercase().contains(&q)
                || p.pid.to_string().contains(&q)
                || p.cmd.to_lowercase().contains(&q)
        })
        .cloned()
        .collect();
    p.sort_by(|a, b| {
        let o = match s.sort {
            Sort::Name => a.name.to_lowercase().cmp(&b.name.to_lowercase()),
            Sort::Pid => a.pid.cmp(&b.pid),
            Sort::Cpu => a
                .cpu
                .partial_cmp(&b.cpu)
                .unwrap_or(std::cmp::Ordering::Equal),
            Sort::Memory => a.rss_kib.cmp(&b.rss_kib),
            Sort::State => a.state.cmp(&b.state),
        };
        if s.ascending {
            o
        } else {
            o.reverse()
        }
    });
    p
}
fn table_page(c: *mut Canvas, s: &mut AppState, x: f32, y: f32, w: f32, h: f32, page: Page) {
    let title = match page {
        Page::Applications => "Applications FLX",
        Page::Services => "Services détectés",
        _ => "Processus et applications",
    };
    draw_text(c, 3, 19.0, x + 12.0, y + 24.0, TXT, title);
    let search_x = x + w * 0.32;
    let result = unsafe {
        ui_field(
            31,
            s.query.as_mut_ptr(),
            s.query.len() as i32,
            search_x,
            y + 5.0,
            (w * 0.38).max(145.0),
            30.0,
            "Rechercher un processus…",
        )
    };
    if result == 2 {
        s.scroll.pos = 0.0
    }
    let filter_label = match s.filter {
        Filter::All => "Tous",
        Filter::Current => "Utilisateur",
        Filter::Running => "En cours",
        Filter::Sleeping => "En veille",
        Filter::System => "Système FLX",
    };
    if unsafe { ui_button(x + w - 112.0, y + 5.0, 102.0, 30.0, filter_label, 0) } {
        s.filter = match s.filter {
            Filter::All => Filter::Current,
            Filter::Current => Filter::Running,
            Filter::Running => Filter::Sleeping,
            Filter::Sleeping => Filter::System,
            Filter::System => Filter::All,
        };
    }
    let hy = y + 44.0;
    unsafe {
        g_rect(c, x, hy, w, 28.0, 0xff051319);
        g_line(c, x, hy + 28.0, x + w, hy + 28.0, 1.0, LINE)
    }
    let cols = [
        x + 14.0,
        x + w * 0.34,
        x + w * 0.44,
        x + w * 0.54,
        x + w * 0.67,
        x + w * 0.80,
        x + w * 0.91,
    ];
    for (i, l) in ["Nom", "PID", "CPU", "Mémoire", "Disque", "Réseau", "Statut"]
        .iter()
        .enumerate()
    {
        draw_text(c, 2, 11.0, cols[i], hy + 18.0, SUB, l);
        let key = match i {
            0 => Sort::Name,
            1 => Sort::Pid,
            2 => Sort::Cpu,
            3 => Sort::Memory,
            _ => Sort::State,
        };
        if unsafe { ui_click(cols[i] - 4.0, hy, w * 0.12, 28.0) } {
            if s.sort == key {
                s.ascending = !s.ascending
            } else {
                s.sort = key;
                s.ascending = false
            }
        }
    }
    let rows_y = hy + 30.0;
    let rh = 34.0;
    let row_area = (y + h - rows_y - 24.0).max(20.0);
    let mut clip = [0i32; 4];
    unsafe {
        g_getclip(c, clip.as_mut_ptr());
        g_clip(c, x, rows_y, x + w, rows_y + row_area)
    }
    let all = visible_processes(s, page);
    let mut content = 0.0;
    for (i, p) in all.iter().enumerate() {
        let ry = rows_y + i as f32 * rh - s.scroll.pos;
        content += rh;
        if ry + rh < rows_y || ry > rows_y + row_area {
            continue;
        }
        let is_selected = s.selected == Some((p.pid, p.start));
        if is_selected {
            unsafe { g_rrect(c, x + 3.0, ry, w - 6.0, rh - 1.0, 4.0, 4.0, 0xff103039) }
        } else if unsafe { ui_hover(x + 3.0, ry, w - 6.0, rh - 1.0) } {
            unsafe { g_rrect(c, x + 3.0, ry, w - 6.0, rh - 1.0, 4.0, 4.0, 0xff081e24) }
        }
        let idx = p
            .exe
            .as_ref()
            .and_then(|v| v.file_name())
            .and_then(|n| n.to_str())
            .map(|n| {
                let cs = cstr(n);
                unsafe { app_by_exe(cs.as_ptr()) }
            })
            .unwrap_or(-1);
        if idx >= 0 {
            unsafe { icon_app(c, idx, x + 14.0, ry + 5.0, 19.0) }
        } else {
            unsafe {
                g_circle(
                    c,
                    x + 23.0,
                    ry + 15.0,
                    4.0,
                    match p.state {
                        'R' => GREEN,
                        'S' => CYAN2,
                        'Z' => RED,
                        'D' => WARNING,
                        _ => MUTED,
                    },
                )
            }
        }
        draw_text(
            c,
            2,
            11.5,
            x + 34.0,
            ry + 19.0,
            if is_selected { TXT } else { SUB },
            &truncate(&p.name, 24),
        );
        draw_text(c, 2, 10.0, cols[1], ry + 19.0, MUTED, &p.pid.to_string());
        draw_text(
            c,
            2,
            11.0,
            cols[2],
            ry + 21.0,
            if p.cpu.unwrap_or(0.0) > 10.0 {
                GOLD
            } else {
                TXT
            },
            &p.cpu
                .map(|v| format!("{v:.1}%"))
                .unwrap_or_else(|| "—".into()),
        );
        draw_text(
            c,
            2,
            11.0,
            cols[3],
            ry + 21.0,
            CYAN2,
            &fmt_bytes(p.rss_kib * 1024),
        );
        draw_text(
            c,
            2,
            10.0,
            cols[4],
            ry + 21.0,
            SUB,
            &format!("{} / {}", fmt_rate(p.read_bps), fmt_rate(p.write_bps)),
        );
        draw_text(c, 2, 10.0, cols[5], ry + 21.0, MUTED, "—");
        draw_text(
            c,
            2,
            10.0,
            cols[6],
            ry + 21.0,
            match p.state {
                'R' => GREEN,
                'S' => SUB,
                'Z' => RED,
                'D' => WARNING,
                _ => MUTED,
            },
            match p.state {
                'R' => "En cours",
                'S' => "En veille",
                'D' => "Attente I/O",
                'Z' => "Zombie",
                'T' => "Arrêté",
                _ => "Inconnu",
            },
        );
        if unsafe { ui_downin(x + 3.0, ry, w - 6.0, rh - 1.0) } {
            s.selected = Some((p.pid, p.start));
            let m = cstr(&format!("TASKMGR select {} {}", p.pid, p.name));
            unsafe { MARK(m.as_ptr()) }
        }
    }
    unsafe {
        g_setclip(c, clip.as_ptr());
        ui_scroll(&mut s.scroll, x, rows_y, w, row_area, content)
    }
    let shown = all.len();
    draw_text(
        c,
        2,
        9.0,
        x + 10.0,
        y + h - 8.0,
        MUTED,
        &format!("{shown} processus · données /proc"),
    );
    if shown == 0 {
        draw_text(
            c,
            2,
            12.0,
            x + w * 0.35,
            rows_y + 50.0,
            MUTED,
            "Aucun processus ne correspond à cette recherche.",
        )
    }
}
fn details_page(c: *mut Canvas, s: &AppState, x: f32, y: f32, w: f32, h: f32) {
    draw_text(c, 3, 20.0, x + 16.0, y + 28.0, TXT, "Détails du processus");
    let Some(p) = selected_process(s) else {
        draw_text(
            c,
            2,
            13.0,
            x + 18.0,
            y + 70.0,
            MUTED,
            "Sélectionnez un processus dans l'onglet Processus.",
        );
        return;
    };
    let exe = p
        .exe
        .as_ref()
        .map(|x| x.to_string_lossy().to_string())
        .unwrap_or_else(|| "Non disponible".into());
    let fields = [
        ("Nom", p.name.clone()),
        ("PID / PPID", format!("{} / {}", p.pid, p.ppid)),
        ("État", state_name(p.state).into()),
        ("UID", p.uid.to_string()),
        ("Commande", p.cmd.clone()),
        ("Exécutable", exe),
        ("CPU", pct(p.cpu)),
        ("Mémoire résidente", fmt_bytes(p.rss_kib * 1024)),
        ("Mémoire virtuelle", fmt_bytes(p.vsize_kib * 1024)),
        ("Threads", p.threads.to_string()),
        ("Lecture disque", fmt_rate(p.read_bps)),
        ("Écriture disque", fmt_rate(p.write_bps)),
        (
            "Réseau par processus",
            "Non disponible · aucune source noyau fiable".into(),
        ),
    ];
    for (i, (k, v)) in fields.iter().enumerate() {
        let col = i / 7;
        let row = i % 7;
        let xx = x + 16.0 + col as f32 * (w * 0.48);
        let yy = y + 64.0 + row as f32 * 42.0;
        box_panel(c, xx, yy, w * 0.45, 35.0);
        draw_text(c, 2, 9.0, xx + 9.0, yy + 13.0, MUTED, k);
        draw_text(c, 2, 10.0, xx + 9.0, yy + 28.0, TXT, &truncate(v, 38));
    }
    let _ = h;
}
fn state_name(s: char) -> &'static str {
    match s {
        'R' => "En cours",
        'S' => "En veille",
        'D' => "Attente non interruptible",
        'Z' => "Zombie",
        'T' | 't' => "Arrêté",
        'I' => "Inactif",
        _ => "Inconnu",
    }
}
fn performance_page(c: *mut Canvas, s: &AppState, x: f32, y: f32, w: f32, h: f32) {
    draw_text(c, 3, 20.0, x + 14.0, y + 27.0, TXT, "Performance");
    let gap = 12.0;
    let half = (w - gap) / 2.0;
    let chart_h = ((h - 110.0) / 2.0).max(125.0);
    chart(
        c,
        x + 10.0,
        y + 44.0,
        half,
        chart_h,
        "CPU · utilisation globale",
        &[(&s.history.cpu, CYAN)],
        true,
    );
    chart(
        c,
        x + 10.0 + half + gap,
        y + 44.0,
        half,
        chart_h,
        "Mémoire · utilisée",
        &[(&s.history.memory, GOLD)],
        true,
    );
    chart(
        c,
        x + 10.0,
        y + 56.0 + chart_h,
        half,
        chart_h,
        "Stockage · lectures agrégées",
        &[(&s.history.disk, CYAN2)],
        false,
    );
    chart(
        c,
        x + 10.0 + half + gap,
        y + 56.0 + chart_h,
        half,
        chart_h,
        "Réseau · réception agrégée",
        &[(&s.history.network, GOLD)],
        false,
    );
    let mut yy = y + 72.0 + chart_h * 2.0;
    draw_text(c, 3, 13.0, x + 14.0, yy, CYAN, "Cœurs logiques");
    yy += 18.0;
    let cw = (w - 34.0) / (s.snapshot.cores.len().max(1).min(8) as f32);
    for (i, v) in s.snapshot.cores.iter().take(8).enumerate() {
        let xx = x + 14.0 + i as f32 * cw;
        draw_text(c, 2, 9.0, xx, yy, MUTED, &format!("CPU{i}  {}", pct(*v)));
        unsafe {
            g_rrect(c, xx, yy + 6.0, cw - 8.0, 4.0, 2.0, 2.0, LINE);
            g_rrect(
                c,
                xx,
                yy + 6.0,
                (cw - 8.0) * v.unwrap_or(0.0).clamp(0.0, 100.0) as f32 / 100.0,
                4.0,
                2.0,
                2.0,
                CYAN,
            )
        }
    }
}
fn simple_info_page(c: *mut Canvas, s: &AppState, page: Page, x: f32, y: f32, w: f32, h: f32) {
    match page {
        Page::Startup => {
            draw_text(c, 3, 20.0, x + 14.0, y + 28.0, TXT, "Démarrage");
            draw_text(
                c,
                2,
                12.0,
                x + 16.0,
                y + 65.0,
                SUB,
                "FLX OS démarre ses composants depuis init (PID 1).",
            );
            draw_text(
                c,
                2,
                11.0,
                x + 16.0,
                y + 90.0,
                MUTED,
                "Aucun gestionnaire de démarrage utilisateur n'est configuré.",
            );
            draw_text(
                c,
                2,
                11.0,
                x + 16.0,
                y + 116.0,
                MUTED,
                "Cette page reste en lecture seule : aucune entrée modifiable n'est exposée.",
            );
        }
        Page::Users => {
            draw_text(
                c,
                3,
                20.0,
                x + 14.0,
                y + 28.0,
                TXT,
                "Utilisateurs et processus",
            );
            let mut sums: HashMap<u32, (usize, u64, f64)> = HashMap::new();
            for p in &s.snapshot.processes {
                let v = sums.entry(p.uid).or_default();
                v.0 += 1;
                v.1 = v.1.saturating_add(p.rss_kib);
                v.2 += p.cpu.unwrap_or(0.0)
            }
            let mut items: Vec<_> = sums.into_iter().collect();
            items.sort_by_key(|x| x.0);
            let mut yy = y + 62.0;
            for (uid, (n, mem, cpu)) in items {
                box_panel(c, x + 12.0, yy, w - 24.0, 44.0);
                draw_text(c, 3, 12.0, x + 24.0, yy + 19.0, TXT, &format!("UID {uid}"));
                draw_text(
                    c,
                    2,
                    10.0,
                    x + 24.0,
                    yy + 35.0,
                    SUB,
                    &format!("{n} processus"),
                );
                draw_text(
                    c,
                    2,
                    10.0,
                    x + w * 0.55,
                    yy + 27.0,
                    CYAN2,
                    &format!("{} · {:.1}% CPU", fmt_bytes(mem * 1024), cpu),
                );
                yy += 50.0;
                if yy > y + h - 20.0 {
                    break;
                }
            }
        }
        Page::Services => {
            draw_text(c, 3, 20.0, x + 14.0, y + 28.0, TXT, "Services FLX détectés");
            draw_text(
                c,
                2,
                11.0,
                x + 16.0,
                y + 52.0,
                MUTED,
                "Lecture seule · FLX OS ne fournit pas de gestionnaire de services.",
            );
            let names = [
                ("init", "PID 1 · initialisation système"),
                ("flx-netd", "Démon réseau FLX"),
                ("flx-compositor", "Compositeur FLX"),
                ("flxsh", "Shell FLX"),
            ];
            let mut yy = y + 82.0;
            for (name, desc) in names {
                if let Some(p) = s.snapshot.processes.iter().find(|p| p.name == name) {
                    box_panel(c, x + 12.0, yy, w - 24.0, 48.0);
                    unsafe {
                        g_circle(
                            c,
                            x + 27.0,
                            yy + 24.0,
                            4.0,
                            if p.state == 'R' { GREEN } else { SUB },
                        )
                    }
                    draw_text(c, 3, 12.0, x + 42.0, yy + 20.0, TXT, name);
                    draw_text(
                        c,
                        2,
                        9.0,
                        x + 42.0,
                        yy + 36.0,
                        SUB,
                        &format!("{desc} · PID {} · {}", p.pid, state_name(p.state)),
                    );
                    yy += 56.0
                }
            }
        }
        Page::Applications | Page::Processes | Page::Performance => {}
        Page::Details => {}
    }
}
fn action_confirm(c: *mut Canvas, s: &mut AppState, w: f32, h: f32) {
    let Some(action) = s.pending else { return };
    unsafe {
        g_rect(c, 0.0, 68.0, w, h - 68.0, 0x99000000);
        let bw = 390.0;
        let bh = 190.0;
        let x = (w - bw) / 2.0;
        let y = (h - bh) / 2.0;
        g_rrect(c, x, y, bw, bh, 10.0, 10.0, CARD);
        g_rrect_line(c, x, y, bw, bh, 10.0, 1.0, LINE)
    }
    let name = selected_process(s)
        .map(|p| p.name.clone())
        .unwrap_or_else(|| "processus disparu".into());
    draw_text(
        c,
        3,
        17.0,
        w / 2.0 - 165.0,
        h / 2.0 - 48.0,
        TXT,
        if matches!(action, Pending::Force) {
            "Forcer l'arrêt ?"
        } else {
            "Terminer le processus ?"
        },
    );
    draw_text(
        c,
        2,
        11.0,
        w / 2.0 - 165.0,
        h / 2.0 - 20.0,
        SUB,
        &format!(
            "{} · le processus peut déjà s'être arrêté.",
            truncate(&name, 36)
        ),
    );
    if unsafe {
        ui_button(
            w / 2.0 - 165.0,
            h / 2.0 + 24.0,
            150.0,
            34.0,
            "Confirmer",
            if matches!(action, Pending::Force) {
                1
            } else {
                0
            },
        )
    } {
        perform_signal(s, action)
    }
    if unsafe { ui_button(w / 2.0 + 14.0, h / 2.0 + 24.0, 150.0, 34.0, "Annuler", 0) }
        || unsafe { U.ev == 6 && U.key == K_ESC }
    {
        s.pending = None
    }
}
fn perform_signal(s: &mut AppState, a: Pending) {
    let Some((pid, start)) = s.selected else {
        s.status = "Sélection invalide.".into();
        s.pending = None;
        return;
    };
    if pid == 1 {
        s.status = "Le processus PID 1 est protégé.".into();
        s.pending = None;
        return;
    }
    let path = format!("/proc/{pid}/stat");
    let identity = fs::read_to_string(path)
        .ok()
        .and_then(|v| parse_proc_stat(&v));
    if !identity.is_some_and(|p| p.start_ticks == start) {
        s.status = "Processus terminé ou identité modifiée ; action annulée.".into();
        s.selected = None;
        s.pending = None;
        return;
    }
    let sig = if matches!(a, Pending::Force) {
        SIGKILL
    } else {
        SIGTERM
    };
    if unsafe { kill(pid as i32, sig) } == 0 {
        let name = s
            .snapshot
            .processes
            .iter()
            .find(|p| p.pid == pid && p.start == start)
            .map(|p| p.name.as_str())
            .unwrap_or("processus");
        let m = cstr(&format!("TASKMGR kill {pid} {name}"));
        unsafe { MARK(m.as_ptr()) };
        s.status = if sig == SIGTERM {
            format!("SIGTERM envoyé au PID {pid}.")
        } else {
            format!("SIGKILL envoyé au PID {pid}.")
        };
        s.pending = None
    } else {
        s.status = format!("Signal refusé : {}", std::io::Error::last_os_error());
        s.pending = None
    }
}
extern "C" fn draw(c: *mut Canvas) {
    let mut s = state().lock().unwrap_or_else(|e| e.into_inner());
    unsafe {
        let w = U.w as f32;
        let h = U.h as f32;
        g_rect(c, 0.0, 0.0, w, h, BG)
    }
    draw_header(c, unsafe { U.w as f32 });
    let h = unsafe { U.h as f32 };
    let w = unsafe { U.w as f32 };
    draw_sidebar(c, &mut s, w, h);
    let main_x = 186.0;
    let right_w = (w * 0.185).clamp(196.0, 250.0);
    let right_x = w - right_w - 12.0;
    let main_w = (right_x - main_x - 10.0).max(280.0);
    let top = 80.0;
    let panel_y = 252.0;
    let panel_h = h - panel_y - 12.0;
    let gap = 8.0;
    let stat_w = (main_w - 4.0 * gap) / 5.0;
    let memory_used = s
        .snapshot
        .memory
        .available_kib
        .map(|a| s.snapshot.memory.total_kib.saturating_sub(a));
    let memory_pct = memory_used
        .filter(|_| s.snapshot.memory.total_kib > 0)
        .map(|v| v as f64 * 100.0 / s.snapshot.memory.total_kib as f64);
    let disk = s.snapshot.filesystem;
    let disk_pct = disk
        .filter(|x| x.total > 0)
        .map(|x| x.used as f64 * 100.0 / x.total as f64);
    let freq = s
        .snapshot
        .cpu_mhz
        .map(|x| format!("{x:.0} MHz"))
        .unwrap_or_else(|| "Fréquence indisponible".into());
    metric_card(
        c,
        main_x,
        top,
        stat_w,
        154.0,
        "CPU",
        &pct(s.snapshot.cpu),
        &freq,
        CYAN,
        s.snapshot.cpu,
        &s.history.cpu,
    );
    metric_card(
        c,
        main_x + stat_w + gap,
        top,
        stat_w,
        154.0,
        "Mémoire",
        &memory_used
            .map(|v| {
                format!(
                    "{} / {}",
                    fmt_bytes(v * 1024),
                    fmt_bytes(s.snapshot.memory.total_kib * 1024)
                )
            })
            .unwrap_or_else(|| "Non disponible".into()),
        "Utilisée",
        CYAN2,
        memory_pct,
        &s.history.memory,
    );
    metric_card(
        c,
        main_x + 2.0 * (stat_w + gap),
        top,
        stat_w,
        154.0,
        "Disque",
        &disk
            .map(|x| format!("{} / {}", fmt_bytes(x.used), fmt_bytes(x.total)))
            .unwrap_or_else(|| "—".into()),
        "Volume / · activité globale",
        GOLD,
        disk_pct,
        &s.history.disk,
    );
    metric_card(
        c,
        main_x + 3.0 * (stat_w + gap),
        top,
        stat_w,
        154.0,
        "Réseau",
        &format!("↓ {}", fmt_rate(s.snapshot.net_rx_bps)),
        &format!("↑ {}", fmt_rate(s.snapshot.net_tx_bps)),
        CYAN,
        None,
        &s.history.network,
    );
    chart(
        c,
        main_x + 4.0 * (stat_w + gap),
        top,
        stat_w,
        154.0,
        "Vue globale",
        &[(&s.history.cpu, CYAN), (&s.history.memory, GOLD)],
        true,
    );
    draw_right(c, &mut s, right_x, top, right_w, h - top - 12.0);
    box_panel(c, main_x, panel_y, main_w, panel_h);
    let content_x = main_x + 8.0;
    let content_y = panel_y + 5.0;
    let content_w = main_w - 16.0;
    let content_h = panel_h - 10.0;
    match s.page {
        Page::Processes => table_page(
            c,
            &mut s,
            content_x,
            content_y,
            content_w,
            content_h,
            Page::Processes,
        ),
        Page::Applications => table_page(
            c,
            &mut s,
            content_x,
            content_y,
            content_w,
            content_h,
            Page::Applications,
        ),
        Page::Services => simple_info_page(
            c,
            &s,
            Page::Services,
            content_x,
            content_y,
            content_w,
            content_h,
        ),
        Page::Startup => simple_info_page(
            c,
            &s,
            Page::Startup,
            content_x,
            content_y,
            content_w,
            content_h,
        ),
        Page::Users => simple_info_page(
            c,
            &s,
            Page::Users,
            content_x,
            content_y,
            content_w,
            content_h,
        ),
        Page::Details => details_page(c, &s, content_x, content_y, content_w, content_h),
        Page::Performance => performance_page(c, &s, content_x, content_y, content_w, content_h),
    }
    if !s.status.is_empty() {
        draw_text(
            c,
            2,
            9.0,
            main_x + 10.0,
            h - 3.0,
            MUTED,
            &truncate(&s.status, 100),
        )
    }
    action_confirm(c, &mut s, w, h);
    if unsafe { U.ev == 6 } {
        match unsafe { U.key } {
            K_DOWN => {
                let list = visible_processes(&s, s.page);
                if !list.is_empty() {
                    let idx = s
                        .selected
                        .and_then(|id| list.iter().position(|p| (p.pid, p.start) == id))
                        .unwrap_or(0);
                    let p = &list[(idx + 1).min(list.len() - 1)];
                    s.selected = Some((p.pid, p.start));
                }
            }
            K_UP => {
                let list = visible_processes(&s, s.page);
                if !list.is_empty() {
                    let idx = s
                        .selected
                        .and_then(|id| list.iter().position(|p| (p.pid, p.start) == id))
                        .unwrap_or(0);
                    let p = &list[idx.saturating_sub(1)];
                    s.selected = Some((p.pid, p.start));
                }
            }
            K_ESC => s.pending = None,
            _ => {}
        }
    }
    unsafe { U.ev = 0 };
}
extern "C" fn main_init(_: i32, _: *mut *mut i8) {
    start_worker()
}
fn main() {
    let mut args: Vec<CString> = std::env::args().map(|s| cstr(&s)).collect();
    let mut argv: Vec<*mut i8> = args.iter_mut().map(|s| s.as_ptr() as *mut i8).collect();
    let app_title = cstr("Gestionnaire de tâches");
    let mut a = AppDef {
        exe: b"taskmgr\0".as_ptr() as _,
        title: app_title.as_ptr(),
        w: 1400,
        h: 820,
        mica: 1,
        init: Some(main_init),
        draw: Some(draw),
        tick: Some(tick),
        tick_ms: 250,
        close: Some(close),
        extra_fd: -1,
        on_fd: None,
    };
    unsafe {
        std::process::exit(app_main(&mut a, argv.len() as i32, argv.as_mut_ptr()));
    }
}
