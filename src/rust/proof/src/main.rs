//! Native FLX system information overview.
use std::{
    cell::RefCell,
    ffi::{c_char, CString},
    fs,
    time::Instant,
};

#[repr(C)]
struct Canvas {
    _opaque: [u8; 0],
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
    exe: *const c_char,
    title: *const c_char,
    w: i32,
    h: i32,
    mica: i32,
    init: Option<extern "C" fn(i32, *mut *mut c_char)>,
    draw: Option<extern "C" fn(*mut Canvas)>,
    tick: Option<extern "C" fn()>,
    tick_ms: i32,
    close: Option<extern "C" fn() -> i32>,
    extra_fd: i32,
    on_fd: Option<extern "C" fn()>,
}
#[repr(C)]
#[derive(Clone, Copy, Default)]
struct FsStats {
    total: u64,
    available: u64,
    used: u64,
}
#[repr(C)]
#[derive(Default)]
struct Scroll {
    pos: f32,
    max: f32,
    drag: i32,
    grab: f32,
}
#[repr(C)]
#[derive(Clone, Copy)]
struct Theme {
    dark: i32,
    accent_i: i32,
    wall: i32,
    accent: u32,
    accent_fg: u32,
    accent_soft: u32,
    bg: u32,
    mica: u32,
    surface: u32,
    card: u32,
    card2: u32,
    hover: u32,
    press: u32,
    line: u32,
    text: u32,
    sub: u32,
    dim: u32,
    sel: u32,
    danger: u32,
}
#[link(name = "w12", kind = "static")]
unsafe extern "C" {
    fn app_main(a: *mut AppDef, argc: i32, argv: *mut *mut c_char) -> i32;
    fn app_redraw();
    fn g_rect(c: *mut Canvas, x: f32, y: f32, w: f32, h: f32, col: u32);
    fn g_rrect(c: *mut Canvas, x: f32, y: f32, w: f32, h: f32, r: f32, rb: f32, col: u32);
    fn g_line(c: *mut Canvas, x: f32, y: f32, x2: f32, y2: f32, lw: f32, col: u32);
    fn g_clip(c: *mut Canvas, x: f32, y: f32, x2: f32, y2: f32);
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
        s: *const c_char,
    ) -> f32;
    fn icon_app(c: *mut Canvas, app: i32, x: f32, y: f32, size: f32);
    fn icon_glyph(c: *mut Canvas, glyph: i32, x: f32, y: f32, size: f32, col: u32);
    fn ui_click(x: f32, y: f32, w: f32, h: f32) -> bool;
    fn ui_hover(x: f32, y: f32, w: f32, h: f32) -> bool;
    fn ui_scroll(s: *mut Scroll, x: f32, y: f32, w: f32, h: f32, content_h: f32);
    fn ui_card(x: f32, y: f32, w: f32, h: f32);
    fn flx_fs_stats(path: *const c_char, out: *mut FsStats) -> i32;
    static mut U: Ui;
    static mut TH: Theme;
}
const REG: i32 = 2;
const SEMI: i32 = 3;
const BOLD: i32 = 4;
#[derive(Clone, Copy, PartialEq)]
enum Page {
    Overview,
    Hardware,
    Software,
    Security,
    Processes,
}
impl Page {
    const ALL: [Page; 5] = [
        Self::Overview,
        Self::Hardware,
        Self::Software,
        Self::Security,
        Self::Processes,
    ];
    fn label(self) -> &'static str {
        match self {
            Self::Overview => "Vue d’ensemble",
            Self::Hardware => "Matériel",
            Self::Software => "Logiciel",
            Self::Security => "Sécurité",
            Self::Processes => "Processus",
        }
    }
    fn glyph(self) -> i32 {
        match self {
            Self::Overview => 39,
            Self::Hardware => 51,
            Self::Software => 42,
            Self::Security => 44,
            Self::Processes => 27,
        }
    }
}
#[derive(Default)]
struct Info {
    cpu: String,
    cores: usize,
    mem_total: u64,
    mem_used: u64,
    swap_total: u64,
    swap_used: u64,
    storage: FsStats,
    kernel: String,
    os: String,
    arch: String,
    uptime: u64,
    processes: Vec<(u32, String, String)>,
}
struct State {
    page: Page,
    info: Info,
    scroll: Scroll,
    sampled: Instant,
}
thread_local! { static STATE: RefCell<Option<State>> = const { RefCell::new(None) }; }
fn read(path: &str) -> String {
    fs::read_to_string(path).unwrap_or_default()
}
fn cstr(s: &str) -> CString {
    CString::new(s.replace('\0', "�")).unwrap_or_default()
}
fn collect() -> Info {
    let mut i = Info::default();
    let ci = read("/proc/cpuinfo");
    i.cpu = ci
        .lines()
        .find_map(|l| {
            l.strip_prefix("model name\t:")
                .or_else(|| l.strip_prefix("Hardware\t:"))
                .map(str::trim)
                .map(str::to_owned)
        })
        .unwrap_or_else(|| "Processeur non identifié".into());
    i.cores = ci
        .lines()
        .filter(|l| l.starts_with("processor\t:"))
        .count()
        .max(1);
    let mi = read("/proc/meminfo");
    let (mut total, mut avail, mut st, mut sf) = (0, 0, 0, 0);
    for l in mi.lines() {
        let mut p = l.split_whitespace();
        let k = p.next().unwrap_or("");
        let v = p.next().and_then(|x| x.parse::<u64>().ok()).unwrap_or(0);
        match k {
            "MemTotal:" => total = v,
            "MemAvailable:" => avail = v,
            "SwapTotal:" => st = v,
            "SwapFree:" => sf = v,
            _ => {}
        }
    }
    i.mem_total = total;
    i.mem_used = total.saturating_sub(avail);
    i.swap_total = st;
    i.swap_used = st.saturating_sub(sf);
    let _ = unsafe { flx_fs_stats(cstr("/").as_ptr(), &mut i.storage) };
    let os = read("/etc/os-release");
    i.os = os
        .lines()
        .find_map(|l| l.strip_prefix("PRETTY_NAME="))
        .map(|s| s.trim_matches('"').to_owned())
        .unwrap_or_else(|| "FLX OS".into());
    i.kernel = read("/proc/version")
        .split_whitespace()
        .take(3)
        .collect::<Vec<_>>()
        .join(" ");
    i.arch = std::env::consts::ARCH.into();
    i.uptime = read("/proc/uptime")
        .split_whitespace()
        .next()
        .and_then(|s| s.split('.').next())
        .and_then(|s| s.parse().ok())
        .unwrap_or(0);
    if let Ok(entries) = fs::read_dir("/proc") {
        for e in entries.flatten() {
            let name = e.file_name().to_string_lossy().into_owned();
            let Ok(pid) = name.parse::<u32>() else {
                continue;
            };
            let comm = fs::read_to_string(format!("/proc/{pid}/comm"))
                .unwrap_or_else(|_| "Processus terminé".into())
                .trim()
                .to_owned();
            let exe = fs::read_link(format!("/proc/{pid}/exe"))
                .map(|p| p.display().to_string())
                .unwrap_or_else(|_| "Accès restreint".into());
            i.processes.push((pid, comm, exe));
        }
    }
    i.processes.sort_by_key(|p| p.0);
    i
}
fn fmt_bytes(n: u64) -> String {
    const UNITS: [&str; 5] = ["o", "Ko", "Mo", "Go", "To"];
    let mut v = n as f64;
    let mut k = 0;
    while v >= 1024.0 && k < UNITS.len() - 1 {
        v /= 1024.0;
        k += 1
    }
    if k == 0 {
        format!("{n} {}", UNITS[k])
    } else {
        format!("{v:.1} {}", UNITS[k])
    }
}
fn fmt_duration(s: u64) -> String {
    let d = s / 86400;
    let h = (s % 86400) / 3600;
    let m = (s % 3600) / 60;
    if d > 0 {
        format!("{d} j {h} h")
    } else if h > 0 {
        format!("{h} h {m} min")
    } else {
        format!("{m} min")
    }
}
fn label(c: *mut Canvas, font: i32, size: f32, x: f32, y: f32, col: u32, s: &str) {
    let q = cstr(s);
    unsafe {
        text(c, font, size, 0.0, x, y, col, q.as_ptr());
    }
}
extern "C" fn init(_: i32, _: *mut *mut c_char) {
    STATE.with(|state| {
        *state.borrow_mut() = Some(State {
            page: Page::Overview,
            info: collect(),
            scroll: Scroll::default(),
            sampled: Instant::now(),
        });
    });
}
extern "C" fn tick() {
    let changed = STATE.with(|state| {
        if let Some(s) = state.borrow_mut().as_mut() {
            if s.sampled.elapsed().as_secs() >= 3 {
                s.info = collect();
                s.sampled = Instant::now();
                return true;
            }
        }
        false
    });
    if changed {
        unsafe { app_redraw() }
    }
}
fn stat_card(
    c: *mut Canvas,
    x: f32,
    y: f32,
    w: f32,
    title: &str,
    value: &str,
    detail: &str,
    glyph: i32,
    t: Theme,
) {
    unsafe {
        ui_card(x, y, w, 116.0);
        icon_glyph(c, glyph, x + 24.0, y + 28.0, 20.0, t.accent);
        label(c, REG, 12.0, x + 44.0, y + 32.0, t.sub, title);
        label(c, BOLD, 24.0, x + 18.0, y + 70.0, t.text, value);
        label(c, REG, 12.0, x + 18.0, y + 96.0, t.dim, detail);
    }
}
fn pair(c: *mut Canvas, x: f32, y: f32, w: f32, name: &str, val: &str, t: Theme) {
    label(c, REG, 13.0, x, y, t.sub, name);
    let q = cstr(val);
    unsafe {
        text(c, SEMI, 13.0, 0.0, x + w, y, t.text, q.as_ptr());
    }
}
extern "C" fn draw(c: *mut Canvas) {
    STATE.with(|state| {
        let mut state = state.borrow_mut();
        unsafe {
            let (w, h) = (U.w as f32, U.h as f32);
            let t = TH;
            let Some(s) = state.as_mut() else { return };
            g_rect(c, 0.0, 0.0, w, h, t.bg);
            g_rect(c, 0.0, 0.0, 248.0, h, t.surface);
            g_line(c, 247.0, 0.0, 247.0, h, 1.0, t.line);
            icon_app(c, 7, 22.0, 17.0, 42.0);
            label(c, SEMI, 20.0, 74.0, 43.0, t.text, "Informations système");
            label(c, REG, 12.0, 74.0, 63.0, t.dim, "État et composants FLX");
            for (n, p) in Page::ALL.into_iter().enumerate() {
                let y = 92.0 + n as f32 * 48.0;
                let active = p == s.page;
                if active {
                    g_rrect(c, 14.0, y, 218.0, 40.0, 10.0, 10.0, t.sel);
                }
                if ui_hover(14.0, y, 218.0, 40.0) {
                    g_rrect(c, 14.0, y, 218.0, 40.0, 10.0, 10.0, t.hover);
                }
                icon_glyph(
                    c,
                    p.glyph(),
                    34.0,
                    y + 20.0,
                    18.0,
                    if active { t.accent } else { t.sub },
                );
                label(
                    c,
                    SEMI,
                    14.0,
                    56.0,
                    y + 25.0,
                    if active { t.text } else { t.sub },
                    p.label(),
                );
                if ui_click(14.0, y, 218.0, 40.0) {
                    s.page = p;
                    s.scroll = Scroll::default();
                }
            }
            let x = 280.0;
            let cw = (w - x - 28.0).max(300.0);
            label(c, BOLD, 25.0, x, 47.0, t.text, s.page.label());
            label(
                c,
                REG,
                13.0,
                x,
                70.0,
                t.dim,
                "Vue claire des ressources et de l’environnement FLX",
            );
            match s.page {
                Page::Overview => {
                    let gap = 14.0;
                    let sw = (cw - 3.0 * gap) / 4.0;
                    let mem = if s.info.mem_total > 0 {
                        format!("{}%", s.info.mem_used * 100 / s.info.mem_total)
                    } else {
                        "—".into()
                    };
                    let disk = if s.info.storage.total > 0 {
                        format!("{}%", s.info.storage.used * 100 / s.info.storage.total)
                    } else {
                        "—".into()
                    };
                    stat_card(
                        c,
                        x,
                        92.0,
                        sw,
                        "PROCESSEUR",
                        &format!("{} cœurs", s.info.cores),
                        &s.info.cpu,
                        52,
                        t,
                    );
                    stat_card(
                        c,
                        x + sw + gap,
                        92.0,
                        sw,
                        "MÉMOIRE",
                        &mem,
                        &format!(
                            "{} / {}",
                            fmt_bytes(s.info.mem_used * 1024),
                            fmt_bytes(s.info.mem_total * 1024)
                        ),
                        53,
                        t,
                    );
                    stat_card(
                        c,
                        x + 2.0 * (sw + gap),
                        92.0,
                        sw,
                        "STOCKAGE",
                        &disk,
                        &format!(
                            "{} / {}",
                            fmt_bytes(s.info.storage.used),
                            fmt_bytes(s.info.storage.total)
                        ),
                        54,
                        t,
                    );
                    stat_card(
                        c,
                        x + 3.0 * (sw + gap),
                        92.0,
                        sw,
                        "PROCESSUS",
                        &s.info.processes.len().to_string(),
                        &format!("Session depuis {}", fmt_duration(s.info.uptime)),
                        27,
                        t,
                    );
                    let top = 226.0;
                    let half = (cw - gap) / 2.0;
                    ui_card(x, top, half, 190.0);
                    icon_glyph(c, 39, x + 22.0, top + 24.0, 18.0, t.accent);
                    label(c, SEMI, 16.0, x + 40.0, top + 29.0, t.text, "Système");
                    pair(
                        c,
                        x + 20.0,
                        top + 70.0,
                        half - 40.0,
                        "Distribution",
                        &s.info.os,
                        t,
                    );
                    pair(
                        c,
                        x + 20.0,
                        top + 104.0,
                        half - 40.0,
                        "Noyau",
                        &s.info.kernel,
                        t,
                    );
                    pair(
                        c,
                        x + 20.0,
                        top + 138.0,
                        half - 40.0,
                        "Architecture",
                        &s.info.arch,
                        t,
                    );
                    pair(
                        c,
                        x + 20.0,
                        top + 172.0,
                        half - 40.0,
                        "Fonctionnement",
                        &fmt_duration(s.info.uptime),
                        t,
                    );
                    let rx = x + half + gap;
                    ui_card(rx, top, half, 190.0);
                    icon_glyph(c, 44, rx + 22.0, top + 24.0, 18.0, t.accent);
                    label(
                        c,
                        SEMI,
                        16.0,
                        rx + 40.0,
                        top + 29.0,
                        t.text,
                        "Sécurité & intégrité",
                    );
                    label(
                        c,
                        REG,
                        13.0,
                        rx + 20.0,
                        top + 67.0,
                        t.sub,
                        "Contrôles disponibles sur cette image système",
                    );
                    let scan = read("/share/proof/scan.txt");
                    let line = scan.lines().next().unwrap_or("Analyse non disponible");
                    let passed = line.starts_with("PASS");
                    g_rrect(
                        c,
                        rx + 20.0,
                        top + 84.0,
                        82.0,
                        28.0,
                        14.0,
                        14.0,
                        if passed { 0xff123d34 } else { t.card2 },
                    );
                    label(
                        c,
                        BOLD,
                        12.0,
                        rx + 37.0,
                        top + 103.0,
                        if passed { 0xff4ed5ad } else { t.sub },
                        if passed { "PASS" } else { "À vérifier" },
                    );
                    label(c, REG, 12.0, rx + 116.0, top + 103.0, t.sub, line);
                    label(
                        c,
                        REG,
                        12.0,
                        rx + 20.0,
                        top + 143.0,
                        t.dim,
                        "Résultat du rapport installé dans l’image.",
                    );
                    label(
                        c,
                        REG,
                        12.0,
                        rx + 20.0,
                        top + 164.0,
                        t.dim,
                        "Aucun statut n’est simulé.",
                    );
                    ui_card(x, top + 204.0, cw, 92.0);
                    label(
                        c,
                        SEMI,
                        15.0,
                        x + 20.0,
                        top + 232.0,
                        t.text,
                        "Environnement FLX",
                    );
                    label(
                        c,
                        REG,
                        13.0,
                        x + 20.0,
                        top + 260.0,
                        t.sub,
                        &format!(
                            "{} processus visibles · {} · {}",
                            s.info.processes.len(),
                            s.info.arch,
                            s.info.kernel
                        ),
                    );
                }
                Page::Hardware => {
                    let rows = vec![
                        (
                            "Processeur",
                            format!("{} ({} cœurs logiques)", s.info.cpu, s.info.cores),
                        ),
                        (
                            "Mémoire vive",
                            format!(
                                "{} utilisés sur {}",
                                fmt_bytes(s.info.mem_used * 1024),
                                fmt_bytes(s.info.mem_total * 1024)
                            ),
                        ),
                        (
                            "Mémoire d’échange",
                            format!(
                                "{} utilisés sur {}",
                                fmt_bytes(s.info.swap_used * 1024),
                                fmt_bytes(s.info.swap_total * 1024)
                            ),
                        ),
                        (
                            "Stockage racine",
                            format!(
                                "{} utilisés sur {} · {} disponibles",
                                fmt_bytes(s.info.storage.used),
                                fmt_bytes(s.info.storage.total),
                                fmt_bytes(s.info.storage.available)
                            ),
                        ),
                        ("Architecture", s.info.arch.clone()),
                    ];
                    let mut y = 92.0;
                    for (a, b) in rows {
                        ui_card(x, y, cw, 76.0);
                        icon_glyph(
                            c,
                            if a == "Processeur" {
                                52
                            } else if a == "Mémoire vive" {
                                53
                            } else {
                                54
                            },
                            x + 24.0,
                            y + 38.0,
                            20.0,
                            t.accent,
                        );
                        label(c, SEMI, 14.0, x + 52.0, y + 32.0, t.text, &a);
                        label(c, REG, 13.0, x + 52.0, y + 55.0, t.sub, &b);
                        y += 88.0
                    }
                }
                Page::Software => {
                    let rows = vec![
                        ("Distribution", s.info.os.clone()),
                        ("Noyau", s.info.kernel.clone()),
                        ("Architecture", s.info.arch.clone()),
                        ("Fonctionnement", fmt_duration(s.info.uptime)),
                        (
                            "Application",
                            "/bin/proof · interface Rust + libflxui".into(),
                        ),
                    ];
                    let mut y = 92.0;
                    for (a, b) in rows {
                        ui_card(x, y, cw, 72.0);
                        label(c, SEMI, 14.0, x + 20.0, y + 29.0, t.text, a);
                        label(c, REG, 13.0, x + 20.0, y + 52.0, t.sub, &b);
                        y += 84.0
                    }
                }
                Page::Security => {
                    ui_card(x, 92.0, cw, 132.0);
                    icon_glyph(c, 44, x + 28.0, 123.0, 24.0, t.accent);
                    label(
                        c,
                        SEMI,
                        17.0,
                        x + 52.0,
                        128.0,
                        t.text,
                        "État des contrôles de build",
                    );
                    let report = read("/share/proof/scan.txt");
                    let line = report.lines().next().unwrap_or("Analyse non disponible");
                    let ok = line.starts_with("PASS");
                    label(
                        c,
                        SEMI,
                        14.0,
                        x + 22.0,
                        176.0,
                        if ok { 0xff4ed5ad } else { t.sub },
                        line,
                    );
                    label(
                        c,
                        REG,
                        12.0,
                        x + 22.0,
                        204.0,
                        t.dim,
                        "Ce rapport ne remplace pas un audit de sécurité.",
                    );
                    ui_card(x, 240.0, cw, 86.0);
                    label(c, SEMI, 14.0, x + 22.0, 271.0, t.text, "Collecte locale");
                    label(
                        c,
                        REG,
                        13.0,
                        x + 22.0,
                        300.0,
                        t.sub,
                        "Données lues depuis /proc et les appels système.",
                    );
                }
                Page::Processes => {
                    let top = 92.0;
                    let hh = (h - top - 18.0).max(120.0);
                    ui_card(x, top, cw, hh);
                    label(
                        c,
                        SEMI,
                        14.0,
                        x + 20.0,
                        top + 27.0,
                        t.text,
                        &format!("Processus observés · {}", s.info.processes.len()),
                    );
                    label(c, REG, 12.0, x + 22.0, top + 53.0, t.dim, "PID");
                    label(c, REG, 12.0, x + 94.0, top + 53.0, t.dim, "NOM");
                    label(c, REG, 12.0, x + cw * 0.43, top + 53.0, t.dim, "EXÉCUTABLE");
                    g_line(
                        c,
                        x + 18.0,
                        top + 64.0,
                        x + cw - 18.0,
                        top + 64.0,
                        1.0,
                        t.line,
                    );
                    let ry = top + 72.0;
                    let rowh = 32.0;
                    let mut clip = [0i32; 4];
                    g_getclip(c, clip.as_mut_ptr());
                    g_clip(c, x + 12.0, ry, x + cw - 12.0, top + hh - 12.0);
                    for (n, (pid, name, exe)) in s.info.processes.iter().enumerate() {
                        let yy = ry + n as f32 * rowh - s.scroll.pos;
                        if yy + rowh < ry || yy > top + hh {
                            continue;
                        }
                        if n % 2 == 0 {
                            g_rrect(c, x + 12.0, yy, cw - 24.0, rowh, 5.0, 5.0, t.card);
                        }
                        label(c, REG, 12.0, x + 22.0, yy + 21.0, t.dim, &pid.to_string());
                        label(c, SEMI, 13.0, x + 94.0, yy + 21.0, t.text, name);
                        label(c, REG, 12.0, x + cw * 0.43, yy + 21.0, t.sub, exe);
                    }
                    g_setclip(c, clip.as_ptr());
                    ui_scroll(
                        &mut s.scroll,
                        x + 12.0,
                        ry,
                        cw - 24.0,
                        hh - (ry - top) - 12.0,
                        s.info.processes.len() as f32 * rowh + 8.0,
                    );
                }
            }
        }
    });
}
extern "C" fn close() -> i32 {
    STATE.with(|state| *state.borrow_mut() = None);
    0
}
fn main() {
    let mut a = AppDef {
        exe: c"proof".as_ptr(),
        title: c"Informations système".as_ptr(),
        w: 1280,
        h: 760,
        mica: 1,
        init: Some(init),
        draw: Some(draw),
        tick: Some(tick),
        tick_ms: 1000,
        close: Some(close),
        extra_fd: -1,
        on_fd: None,
    };
    unsafe {
        std::process::exit(app_main(&mut a, 0, std::ptr::null_mut()));
    }
}
