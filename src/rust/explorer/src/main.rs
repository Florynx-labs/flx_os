use flx_core::{list_dir, parent, Entry};
use std::{
    ffi::CString,
    fs,
    os::unix::fs::PermissionsExt,
    path::{Path, PathBuf},
    sync::{Mutex, OnceLock},
    time::UNIX_EPOCH,
};

#[repr(C)]
struct Canvas {
    _private: [u8; 0],
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
#[link(name = "w12", kind = "static")]
unsafe extern "C" {
    fn app_main(a: *mut AppDef, argc: i32, argv: *mut *mut i8) -> i32;
    fn g_rect(c: *mut Canvas, x: f32, y: f32, w: f32, h: f32, col: u32);
    fn g_rrect(c: *mut Canvas, x: f32, y: f32, w: f32, h: f32, rt: f32, rb: f32, col: u32);
    fn g_rrect_line(c: *mut Canvas, x: f32, y: f32, w: f32, h: f32, r: f32, lw: f32, col: u32);
    fn g_line(c: *mut Canvas, x0: f32, y0: f32, x1: f32, y1: f32, lw: f32, col: u32);
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
    fn ui_click(x: f32, y: f32, w: f32, h: f32) -> bool;
    fn ui_hover(x: f32, y: f32, w: f32, h: f32) -> bool;
    fn ui_downin(x: f32, y: f32, w: f32, h: f32) -> bool;
    fn ui_dblclick(x: f32, y: f32, w: f32, h: f32) -> bool;
    fn ui_button(x: f32, y: f32, w: f32, h: f32, label: *const i8, kind: i32) -> bool;
    fn ui_icon_btn(x: f32, y: f32, w: f32, h: f32, glyph: i32, active: i32) -> bool;
    fn ui_field(
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
    fn g_getclip(c: *mut Canvas, out: *mut i32);
    fn g_clip(c: *mut Canvas, x0: f32, y0: f32, x1: f32, y1: f32);
    fn g_setclip(c: *mut Canvas, clip: *const i32);
    fn icon_glyph(c: *mut Canvas, glyph: i32, cx: f32, cy: f32, size: f32, col: u32);
    fn icon_folder(c: *mut Canvas, x: f32, y: f32, size: f32);
    fn icon_file(c: *mut Canvas, name: *const i8, x: f32, y: f32, size: f32);
    fn icon_logo(c: *mut Canvas, cx: f32, cy: f32, size: f32, alpha: i32);
    fn app_launch(exe: *const i8, arg: *const i8);
    fn app_recent(path: *const i8);
    fn app_by_exe(exe: *const i8) -> i32;
    fn flx_fs_stats(path: *const i8, out: *mut FsStats) -> i32;
    fn localtime_r(timep: *const i64, result: *mut Tm) -> *mut Tm;
    fn strftime(out: *mut i8, max: usize, fmt: *const i8, tm: *const Tm) -> usize;
    static mut U: Ui;
}
#[repr(C)]
struct Tm {
    sec: i32,
    min: i32,
    hour: i32,
    mday: i32,
    mon: i32,
    year: i32,
    wday: i32,
    yday: i32,
    isdst: i32,
    gmtoff: i64,
    zone: *const i8,
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

const BG: u32 = 0xff020c10;
const PANEL: u32 = 0xff031116;
const SIDE: u32 = 0xff04151a;
const HOVER: u32 = 0xff0a2630;
const SELECT: u32 = 0xff073746;
const CYAN: u32 = 0xff28dde7;
const GOLD: u32 = 0xffffb82e;
const TXT: u32 = 0xffeaf5f8;
const SUB: u32 = 0xffa4c3cf;
const MUTED: u32 = 0xff688b98;
const BORDER: u32 = 0xff0b3039;

#[derive(Clone, Copy, PartialEq)]
enum Sort {
    Name,
    Type,
    Size,
    Modified,
}
#[derive(Clone, Copy, PartialEq)]
enum Modal {
    None,
    NewFolder,
    Rename,
    Delete,
}

struct State {
    home: PathBuf,
    path: PathBuf,
    items: Vec<Entry>,
    selected: Option<usize>,
    back: Vec<PathBuf>,
    forward: Vec<PathBuf>,
    tabs: Vec<PathBuf>,
    active_tab: usize,
    search: [i8; 256],
    path_entry: [i8; 1024],
    modal_entry: [i8; 256],
    modal: Modal,
    show_hidden: bool,
    grid: bool,
    sort: Sort,
    descending: bool,
    scroll: Scroll,
    status: String,
    clipboard: Vec<PathBuf>,
    cut: bool,
}
impl State {
    fn new() -> Self {
        let home = std::env::var_os("HOME")
            .map(PathBuf::from)
            .filter(|p| p.is_absolute())
            .unwrap_or_else(|| PathBuf::from("/home/user"));
        let mut s = Self {
            path: home.clone(),
            home: home.clone(),
            items: Vec::new(),
            selected: None,
            back: Vec::new(),
            forward: Vec::new(),
            tabs: vec![home.clone()],
            active_tab: 0,
            search: [0; 256],
            path_entry: [0; 1024],
            modal_entry: [0; 256],
            modal: Modal::None,
            show_hidden: false,
            grid: false,
            sort: Sort::Name,
            descending: false,
            scroll: Scroll::default(),
            status: String::new(),
            clipboard: Vec::new(),
            cut: false,
        };
        s.reload();
        s
    }
    fn reload(&mut self) {
        match list_dir(&self.path, self.show_hidden) {
            Ok(mut entries) => {
                entries.sort_by(|a, b| {
                    let dir = b.is_dir.cmp(&a.is_dir);
                    if dir != std::cmp::Ordering::Equal {
                        return dir;
                    }
                    let o = match self.sort {
                        Sort::Name => a.name.to_lowercase().cmp(&b.name.to_lowercase()),
                        Sort::Type => kind_label(a)
                            .cmp(kind_label(b))
                            .then_with(|| a.name.to_lowercase().cmp(&b.name.to_lowercase())),
                        Sort::Size => a.size.cmp(&b.size),
                        Sort::Modified => a.modified.cmp(&b.modified),
                    };
                    if self.descending {
                        o.reverse()
                    } else {
                        o
                    }
                });
                self.items = entries;
                self.selected = None;
                self.scroll.pos = 0.0;
                self.status = format!("{} éléments", self.items.len());
                write_buf(&mut self.path_entry, &self.path.display().to_string());
            }
            Err(e) => self.status = format!("Impossible d’ouvrir {} : {}", self.path.display(), e),
        }
    }
    fn navigate(&mut self, path: PathBuf, remember: bool) {
        let target = if path.is_absolute() {
            path
        } else {
            self.path.join(path)
        };
        match fs::canonicalize(&target) {
            Ok(path) if path.is_dir() => {
                if remember && path != self.path {
                    self.back.push(self.path.clone());
                    self.forward.clear();
                }
                self.path = path;
                self.tabs[self.active_tab] = self.path.clone();
                self.reload();
            }
            Ok(_) => self.status = "Ce chemin n’est pas un dossier.".into(),
            Err(e) => self.status = format!("Chemin inaccessible : {}", e),
        }
    }
    fn selected_path(&self) -> Option<&Path> {
        self.items.get(self.selected?).map(|e| e.path.as_path())
    }
    fn open_tab(&mut self, path: PathBuf) {
        let target = fs::canonicalize(&path).unwrap_or_else(|_| parent(&path));
        if !target.is_dir() {
            self.status = "Le dossier de destination n’existe pas.".into();
            return;
        }
        self.tabs.push(target.clone());
        self.active_tab = self.tabs.len() - 1;
        self.path = target;
        self.back.clear();
        self.forward.clear();
        self.reload();
    }
}
static STATE: OnceLock<Mutex<State>> = OnceLock::new();
fn state() -> &'static Mutex<State> {
    STATE.get_or_init(|| Mutex::new(State::new()))
}
fn cstr(s: &str) -> CString {
    CString::new(s.replace('\0', "�")).unwrap_or_default()
}
fn write_buf(dst: &mut [i8], s: &str) {
    dst.fill(0);
    let bytes = s.as_bytes();
    let n = bytes.len().min(dst.len().saturating_sub(1));
    for (d, b) in dst.iter_mut().zip(bytes.iter()).take(n) {
        *d = *b as i8;
    }
}
fn read_buf(src: &[i8]) -> String {
    let bytes: Vec<u8> = src
        .iter()
        .take_while(|b| **b != 0)
        .map(|b| *b as u8)
        .collect();
    String::from_utf8_lossy(&bytes).into_owned()
}
fn label(c: *mut Canvas, x: f32, y: f32, font: i32, size: f32, color: u32, value: &str) {
    let v = cstr(value);
    unsafe { text(c, font, size, 0.0, x, y, color, v.as_ptr()) };
}
fn button(_c: *mut Canvas, x: f32, y: f32, w: f32, h: f32, value: &str, kind: i32) -> bool {
    let v = cstr(value);
    unsafe { ui_button(x, y, w, h, v.as_ptr(), kind) }
}
fn icon_file_draw(c: *mut Canvas, name: &str, x: f32, y: f32, size: f32) {
    let n = cstr(name);
    unsafe { icon_file(c, n.as_ptr(), x, y, size) }
}
fn kind_label(e: &Entry) -> &'static str {
    if e.is_dir {
        return "Dossier";
    }
    match e
        .path
        .extension()
        .and_then(|x| x.to_str())
        .unwrap_or("")
        .to_ascii_lowercase()
        .as_str()
    {
        "txt" | "md" | "log" | "csv" => "Texte",
        "pdf" => "PDF",
        "png" | "jpg" | "jpeg" | "gif" | "webp" | "svg" => "Image",
        "mp3" | "wav" | "ogg" | "flac" => "Audio",
        "mp4" | "mkv" | "webm" | "mov" => "Vidéo",
        "zip" | "tar" | "gz" | "xz" | "7z" => "Archive",
        "rs" | "c" | "h" | "py" | "js" | "ts" | "sh" => "Code source",
        _ => "Fichier",
    }
}
fn human_size(n: u64) -> String {
    if n < 1024 {
        format!("{n} o")
    } else if n < 1024 * 1024 {
        format!("{:.1} Ko", n as f64 / 1024.0)
    } else if n < 1024 * 1024 * 1024 {
        format!("{:.1} Mo", n as f64 / (1024.0 * 1024.0))
    } else {
        format!("{:.1} Go", n as f64 / (1024.0 * 1024.0 * 1024.0))
    }
}
fn modified(e: &Entry) -> String {
    let Some(seconds) = e
        .modified
        .and_then(|t| t.duration_since(UNIX_EPOCH).ok())
        .map(|d| d.as_secs() as i64)
    else {
        return "—".into();
    };
    let mut tm = std::mem::MaybeUninit::<Tm>::uninit();
    let mut out = [0i8; 64];
    let fmt = cstr("%d/%m/%Y %H:%M");
    unsafe {
        if localtime_r(&seconds, tm.as_mut_ptr()).is_null() {
            return "—".into();
        }
        if strftime(out.as_mut_ptr(), out.len(), fmt.as_ptr(), tm.as_ptr()) == 0 {
            return "—".into();
        }
    }
    read_buf(&out)
}
fn open_selected(s: &mut State) {
    let Some(e) = s.selected.and_then(|i| s.items.get(i)).cloned() else {
        s.status = "Sélectionnez un élément.".into();
        return;
    };
    if e.is_dir {
        s.navigate(e.path, true);
        return;
    }
    if e.path.parent() == Some(Path::new("/bin")) {
        if let Some(name) = e.path.file_name().and_then(|n| n.to_str()) {
            let exe = cstr(name);
            if unsafe { app_by_exe(exe.as_ptr()) >= 0 } {
                unsafe {
                    app_launch(exe.as_ptr(), cstr("").as_ptr());
                }
                s.status = format!("Application lancée : {}", name);
                return;
            }
        }
    }
    let ext = e
        .path
        .extension()
        .and_then(|x| x.to_str())
        .unwrap_or("")
        .to_ascii_lowercase();
    if ext == "txt"
        || ext == "md"
        || ext == "log"
        || ext == "rs"
        || ext == "c"
        || ext == "h"
        || ext == "json"
        || ext == "toml"
        || ext == "conf"
    {
        let exe = cstr("notepad");
        let arg = cstr(&e.path.to_string_lossy());
        unsafe {
            app_launch(exe.as_ptr(), arg.as_ptr());
            app_recent(arg.as_ptr());
        }
        s.status = format!("Ouvert dans l’Éditeur : {}", e.name);
    } else if e
        .path
        .metadata()
        .map(|m| m.permissions().mode() & 0o111 != 0)
        .unwrap_or(false)
    {
        s.status =
            "Exécutable : lancement automatique bloqué. Utilisez le Terminal explicitement.".into();
    } else {
        s.status = format!("Aucune application associée au type {}.", kind_label(&e));
    }
}
fn copy_tree(source: &Path, target: &Path) -> std::io::Result<()> {
    let meta = fs::symlink_metadata(source)?;
    if meta.file_type().is_symlink() {
        return Err(std::io::Error::new(
            std::io::ErrorKind::Unsupported,
            "les liens symboliques ne sont pas copiés",
        ));
    }
    if meta.is_dir() {
        fs::create_dir(target)?;
        for item in fs::read_dir(source)? {
            let item = item?;
            copy_tree(&item.path(), &target.join(item.file_name()))?;
        }
        fs::set_permissions(target, meta.permissions())?;
    } else {
        fs::copy(source, target)?;
    }
    Ok(())
}
fn paste(s: &mut State) {
    if s.clipboard.is_empty() {
        s.status = "Le presse-papiers de fichiers est vide.".into();
        return;
    }
    let sources = s.clipboard.clone();
    let mut copies = Vec::new();
    for src in &sources {
        let Some(name) = src.file_name() else {
            continue;
        };
        let dst = s.path.join(name);
        if dst.exists() {
            s.status = format!("Conflit : {} existe déjà.", name.to_string_lossy());
            return;
        }
        if s.cut && (src == &s.path || s.path.starts_with(src)) {
            s.status = "Impossible de déplacer un dossier dans lui-même.".into();
            return;
        }
        copies.push((src.clone(), dst));
    }
    let mut copied: Vec<PathBuf> = Vec::new();
    for (src, dst) in &copies {
        if let Err(e) = copy_tree(src, dst) {
            for path in copied.iter().chain(std::iter::once(dst)) {
                let _ = delete_tree(path);
            }
            s.status = format!("Copie interrompue : {}", e);
            s.reload();
            return;
        }
        copied.push(dst.clone());
    }
    if s.cut {
        for (src, _) in &copies {
            if let Err(e) = delete_tree(src) {
                s.status = format!("Destination copiée, source conservée : {}", e);
                s.reload();
                return;
            }
        }
    }
    if s.cut {
        s.clipboard.clear();
        s.cut = false;
    }
    s.status = format!("{} élément(s) collé(s).", sources.len());
    s.reload();
}
fn delete_tree(path: &Path) -> std::io::Result<()> {
    let meta = fs::symlink_metadata(path)?;
    if meta.file_type().is_symlink() {
        return fs::remove_file(path);
    }
    if meta.is_dir() {
        fs::remove_dir_all(path)
    } else {
        fs::remove_file(path)
    }
}
extern "C" fn init(_: i32, _: *mut *mut i8) {
    let _ = state();
}
extern "C" fn tick() {}
extern "C" fn close() -> i32 {
    1
}

extern "C" fn draw(c: *mut Canvas) {
    let mut s = state().lock().unwrap_or_else(|e| e.into_inner());
    let (w, h) = unsafe { (U.w as f32, U.h as f32) };
    unsafe {
        g_rect(c, 0.0, 0.0, w, h, BG);
        g_rect(c, 0.0, 0.0, w, 58.0, PANEL);
    }
    unsafe {
        icon_folder(c, 18.0, 13.0, 31.0);
    }
    label(c, 58.0, 31.0, 3, 19.0, TXT, "Explorateur de fichiers");
    label(
        c,
        58.0,
        48.0,
        2,
        10.0,
        SUB,
        "Accédez à vos fichiers et dossiers",
    );
    for i in 0..s.tabs.len().min(6) {
        let x = 390.0 + i as f32 * 112.0;
        let active = i == s.active_tab;
        let name = s.tabs[i]
            .file_name()
            .and_then(|n| n.to_str())
            .unwrap_or("Accueil");
        unsafe {
            g_rrect(
                c,
                x,
                14.0,
                104.0,
                29.0,
                5.0,
                5.0,
                if active { SELECT } else { SIDE },
            );
        }
        label(
            c,
            x + 9.0,
            32.0,
            2,
            10.0,
            if active { TXT } else { SUB },
            name,
        );
        if s.tabs.len() > 1 {
            label(c, x + 89.0, 32.0, 2, 11.0, MUTED, "×");
        }
        if unsafe { ui_click(x, 14.0, 104.0, 29.0) } {
            s.active_tab = i;
            s.path = s.tabs[i].clone();
            s.back.clear();
            s.forward.clear();
            s.reload();
        }
        if s.tabs.len() > 1 && unsafe { ui_click(x + 86.0, 14.0, 16.0, 29.0) } {
            s.tabs.remove(i);
            if i < s.active_tab {
                s.active_tab -= 1;
            } else if i == s.active_tab {
                s.active_tab = i.min(s.tabs.len() - 1);
            }
            s.path = s.tabs[s.active_tab].clone();
            s.back.clear();
            s.forward.clear();
            s.reload();
            break;
        }
    }

    let nav_w = 214.0;
    let right_w = (w * 0.23).clamp(225.0, 300.0);
    let right_x = w - right_w - 10.0;
    let list_x = nav_w + 10.0;
    let list_w = right_x - list_x - 10.0;
    let toolbar_y = 66.0;
    if unsafe { ui_icon_btn(list_x, toolbar_y, 32.0, 31.0, 14, 0) } {
        if let Some(p) = s.back.pop() {
            let current = s.path.clone();
            s.forward.push(current);
            s.path = p;
            let index = s.active_tab;
            let path = s.path.clone();
            s.tabs[index] = path;
            s.reload();
        }
    }
    if unsafe { ui_icon_btn(list_x + 36.0, toolbar_y, 32.0, 31.0, 15, 0) } {
        if let Some(p) = s.forward.pop() {
            let current = s.path.clone();
            s.back.push(current);
            s.path = p;
            let index = s.active_tab;
            let path = s.path.clone();
            s.tabs[index] = path;
            s.reload();
        }
    }
    if unsafe { ui_icon_btn(list_x + 72.0, toolbar_y, 32.0, 31.0, 16, 0) } {
        if s.path != Path::new("/") {
            let up = parent(&s.path);
            s.navigate(up, true);
        }
    }
    if unsafe { ui_icon_btn(list_x + 108.0, toolbar_y, 32.0, 31.0, 26, 0) } {
        let home = s.home.clone();
        s.navigate(home, true);
    }
    let path_x = list_x + 146.0;
    let path_w = (list_w * 0.48).max(160.0);
    unsafe {
        g_rrect(c, path_x, toolbar_y, path_w, 31.0, 6.0, 6.0, 0xff061b21);
        g_rrect_line(c, path_x, toolbar_y, path_w, 31.0, 6.0, 1.0, BORDER);
    }
    // Render path over its background (kept editable via the field API)
    let path_hint = cstr("Chemin…");
    let path_enter = unsafe {
        ui_field(
            3,
            s.path_entry.as_mut_ptr(),
            s.path_entry.len() as i32,
            path_x,
            toolbar_y,
            path_w,
            31.0,
            path_hint.as_ptr(),
        )
    };
    if path_enter == 1 {
        let p = PathBuf::from(read_buf(&s.path_entry));
        s.navigate(p, true);
    }

    let search_x = path_x + path_w + 8.0;
    let search_w = (list_x + list_w - search_x - 46.0).max(90.0);
    let search_hint = cstr("Rechercher dans ce dossier…");
    let _ = unsafe {
        ui_field(
            4,
            s.search.as_mut_ptr(),
            s.search.len() as i32,
            search_x,
            toolbar_y,
            search_w,
            31.0,
            search_hint.as_ptr(),
        )
    };
    let query = read_buf(&s.search).to_lowercase();
    if unsafe {
        ui_icon_btn(
            list_x + list_w - 34.0,
            toolbar_y,
            32.0,
            31.0,
            if s.grid { 27 } else { 28 },
            s.grid as i32,
        )
    } {
        s.grid = !s.grid;
        s.scroll.pos = 0.0;
    }

    let body_y = toolbar_y + 40.0;
    let footer_y = h - 34.0;
    let body_h = footer_y - body_y;
    unsafe {
        g_rrect(c, 0.0, body_y, nav_w, body_h, 0.0, 0.0, SIDE);
        g_line(c, nav_w, body_y, nav_w, footer_y, 1.0, BORDER);
    }
    let homes = [
        ("Accueil", None, 26),
        ("Bureau", Some("Desktop"), 43),
        ("Documents", Some("Documents"), 22),
        ("Images", Some("Pictures"), 40),
        ("Musique", Some("Music"), 3),
        ("Vidéos", Some("Videos"), 43),
        ("Téléchargements", Some("Downloads"), 49),
    ];
    for (i, (name, subdir, glyph)) in homes.iter().enumerate() {
        let y = body_y + 8.0 + i as f32 * 34.0;
        let target = subdir
            .map(|d| s.home.join(d))
            .unwrap_or_else(|| s.home.clone());
        let selected = s.path == target;
        if selected {
            unsafe {
                g_rrect(c, 8.0, y, nav_w - 16.0, 30.0, 5.0, 5.0, SELECT);
                g_rrect(c, 8.0, y, 3.0, 30.0, 2.0, 2.0, GOLD);
            }
        } else if unsafe { ui_hover(8.0, y, nav_w - 16.0, 30.0) } {
            unsafe {
                g_rrect(c, 8.0, y, nav_w - 16.0, 30.0, 5.0, 5.0, HOVER);
            }
        }
        unsafe {
            icon_glyph(
                c,
                *glyph as i32,
                26.0,
                y + 15.0,
                15.0,
                if selected { GOLD } else { SUB },
            );
        }
        label(
            c,
            46.0,
            y + 20.0,
            2,
            12.0,
            if selected { GOLD } else { TXT },
            name,
        );
        if unsafe { ui_click(8.0, y, nav_w - 16.0, 30.0) } {
            s.navigate(target, true);
        }
    }
    let sep = body_y + 8.0 + homes.len() as f32 * 34.0 + 9.0;
    label(c, 14.0, sep + 10.0, 3, 10.0, MUTED, "PÉRIPHÉRIQUES");
    let croot = cstr("/");
    let mut stats = FsStats::default();
    let has_stats = unsafe { flx_fs_stats(croot.as_ptr(), &mut stats) == 0 && stats.total > 0 };
    unsafe {
        icon_glyph(c, 49, 27.0, sep + 34.0, 16.0, CYAN);
    }
    label(c, 47.0, sep + 37.0, 2, 11.0, TXT, "Disque local (/)");
    let vol = if has_stats {
        format!("{} / {}", human_size(stats.used), human_size(stats.total))
    } else {
        "Capacité indisponible".into()
    };
    label(c, 47.0, sep + 55.0, 2, 10.0, CYAN, &vol);
    if has_stats {
        unsafe {
            g_rrect(c, 47.0, sep + 61.0, 145.0, 4.0, 2.0, 2.0, BORDER);
            g_rrect(
                c,
                47.0,
                sep + 61.0,
                145.0 * (stats.used as f32 / stats.total as f32).clamp(0.0, 1.0),
                4.0,
                2.0,
                2.0,
                CYAN,
            );
        }
    }
    label(c, 14.0, sep + 88.0, 3, 10.0, MUTED, "RÉSEAU");
    unsafe {
        icon_glyph(c, 41, 27.0, sep + 111.0, 15.0, SUB);
    }
    label(
        c,
        47.0,
        sep + 114.0,
        2,
        11.0,
        SUB,
        "Partages non configurés",
    );
    unsafe {
        icon_logo(c, 34.0, footer_y - 18.0, 25.0, 255);
    }
    label(c, 56.0, footer_y - 20.0, 3, 10.0, TXT, "FLX OS");
    label(c, 56.0, footer_y - 6.0, 2, 9.0, MUTED, "File Explorer v1.0");

    unsafe {
        g_rrect(c, list_x, body_y, list_w, body_h, 7.0, 7.0, PANEL);
        g_rrect_line(c, list_x, body_y, list_w, body_h, 7.0, 1.0, BORDER);
    }
    let header_y = body_y + 8.0;
    let row_y = header_y + 29.0;
    let cols = [
        list_x + 14.0,
        list_x + list_w * 0.52,
        list_x + list_w * 0.69,
        list_x + list_w * 0.84,
    ];
    if !s.grid {
        for (i, (label_text, sort)) in [
            ("Nom", Sort::Name),
            ("Type", Sort::Type),
            ("Taille", Sort::Size),
            ("Modifié", Sort::Modified),
        ]
        .iter()
        .enumerate()
        {
            label(c, cols[i], header_y + 19.0, 2, 10.0, SUB, label_text);
            if unsafe {
                ui_click(
                    cols[i] - 4.0,
                    header_y,
                    if i == 0 { list_w * 0.48 } else { list_w * 0.16 },
                    26.0,
                )
            } {
                if s.sort == *sort {
                    s.descending = !s.descending;
                } else {
                    s.sort = *sort;
                    s.descending = false;
                }
                s.reload();
            }
        }
    }
    let matches: Vec<usize> = s
        .items
        .iter()
        .enumerate()
        .filter(|(_, e)| query.is_empty() || e.name.to_lowercase().contains(&query))
        .map(|(i, _)| i)
        .collect();
    let row_h = 37.0;
    let table_h = (footer_y - row_y - 6.0).max(30.0);
    let mut clip = [0; 4];
    unsafe {
        g_getclip(c, clip.as_mut_ptr());
        g_clip(c, list_x, row_y, list_x + list_w, footer_y - 3.0);
    }
    let mut count = 0usize;
    if s.grid {
        let cell_w = 112.0f32;
        let cell_h = 106.0f32;
        let cols_n = ((list_w - 16.0) / cell_w).floor().max(1.0) as usize;
        for (visible, idx) in matches.iter().enumerate() {
            let e = &s.items[*idx];
            let col = visible % cols_n;
            let row = visible / cols_n;
            let x = list_x + 8.0 + col as f32 * cell_w;
            let y = row_y + row as f32 * cell_h - s.scroll.pos;
            count += 1;
            if y + cell_h < row_y || y > footer_y {
                continue;
            }
            let selected = s.selected == Some(*idx);
            unsafe {
                g_rrect(
                    c,
                    x + 3.0,
                    y + 3.0,
                    cell_w - 8.0,
                    cell_h - 8.0,
                    6.0,
                    6.0,
                    if selected {
                        SELECT
                    } else if ui_hover(x + 3.0, y + 3.0, cell_w - 8.0, cell_h - 8.0) {
                        HOVER
                    } else {
                        PANEL
                    },
                );
                if e.is_dir {
                    icon_folder(c, x + cell_w / 2.0 - 14.0, y + 12.0, 30.0);
                } else {
                    icon_file_draw(c, &e.name, x + cell_w / 2.0 - 14.0, y + 12.0, 30.0);
                }
            }
            let name = if e.name.chars().count() > 15 {
                format!("{}…", e.name.chars().take(14).collect::<String>())
            } else {
                e.name.clone()
            };
            label(c, x + 8.0, y + 68.0, 2, 10.0, TXT, &name);
            if unsafe { ui_dblclick(x + 3.0, y + 3.0, cell_w - 8.0, cell_h - 8.0) } {
                s.selected = Some(*idx);
                open_selected(&mut s);
            } else if unsafe { ui_downin(x + 3.0, y + 3.0, cell_w - 8.0, cell_h - 8.0) } {
                s.selected = Some(*idx);
            }
        }
    } else {
        for (visible, idx) in matches.iter().enumerate() {
            let e = &s.items[*idx];
            let y = row_y + visible as f32 * row_h - s.scroll.pos;
            count += 1;
            if y + row_h < row_y || y > footer_y {
                continue;
            }
            if s.selected == Some(*idx) {
                unsafe {
                    g_rrect(
                        c,
                        list_x + 4.0,
                        y,
                        list_w - 8.0,
                        row_h - 1.0,
                        4.0,
                        4.0,
                        SELECT,
                    );
                }
            } else if unsafe { ui_hover(list_x + 4.0, y, list_w - 8.0, row_h - 1.0) } {
                unsafe {
                    g_rrect(
                        c,
                        list_x + 4.0,
                        y,
                        list_w - 8.0,
                        row_h - 1.0,
                        4.0,
                        4.0,
                        HOVER,
                    );
                }
            }
            if e.is_dir {
                unsafe {
                    icon_folder(c, list_x + 12.0, y + 7.0, 23.0);
                }
            } else {
                icon_file_draw(c, &e.name, list_x + 12.0, y + 7.0, 23.0);
            }
            label(c, list_x + 45.0, y + 23.0, 2, 11.0, TXT, &e.name);
            label(c, cols[1], y + 23.0, 2, 10.0, SUB, kind_label(e));
            let size_value = if e.is_dir {
                "—".to_string()
            } else {
                human_size(e.size)
            };
            label(c, cols[2], y + 23.0, 2, 10.0, SUB, &size_value);
            label(c, cols[3], y + 23.0, 2, 9.0, MUTED, &modified(e));
            let hit = unsafe { ui_dblclick(list_x + 4.0, y, list_w - 8.0, row_h) };
            if hit {
                s.selected = Some(*idx);
                open_selected(&mut s);
            } else if unsafe { ui_downin(list_x + 4.0, y, list_w - 8.0, row_h) } {
                s.selected = Some(*idx);
            }
        }
    }
    unsafe {
        g_setclip(c, clip.as_ptr());
    }
    unsafe {
        ui_scroll(
            &mut s.scroll,
            list_x,
            row_y,
            list_w,
            table_h,
            if s.grid {
                ((count + (((list_w - 16.0) / 112.0).floor().max(1.0) as usize) - 1)
                    / (((list_w - 16.0) / 112.0).floor().max(1.0) as usize)) as f32
                    * 106.0
                    + 8.0
            } else {
                count as f32 * row_h + 8.0
            },
        );
    }
    if matches.is_empty() {
        label(
            c,
            list_x + 20.0,
            row_y + 35.0,
            2,
            12.0,
            MUTED,
            if query.is_empty() {
                "Ce dossier est vide."
            } else {
                "Aucun résultat."
            },
        );
    }

    let details_x = right_x;
    unsafe {
        g_rrect(c, details_x, body_y, right_w, body_h, 7.0, 7.0, PANEL);
        g_rrect_line(c, details_x, body_y, right_w, body_h, 7.0, 1.0, BORDER);
    }
    if let Some(e) = s.selected.and_then(|i| s.items.get(i)).cloned() {
        if e.is_dir {
            unsafe {
                icon_folder(c, details_x + 16.0, body_y + 16.0, 42.0);
            }
        } else {
            icon_file_draw(c, &e.name, details_x + 16.0, body_y + 16.0, 42.0);
        }
        label(c, details_x + 18.0, body_y + 80.0, 3, 14.0, TXT, &e.name);
        label(
            c,
            details_x + 18.0,
            body_y + 100.0,
            2,
            10.0,
            CYAN,
            kind_label(&e),
        );
        unsafe {
            g_line(
                c,
                details_x + 14.0,
                body_y + 115.0,
                details_x + right_w - 14.0,
                body_y + 115.0,
                1.0,
                BORDER,
            );
        }
        let rows = [
            ("Emplacement", s.path.display().to_string()),
            (
                "Taille",
                if e.is_dir {
                    "Dossier".into()
                } else {
                    human_size(e.size)
                },
            ),
            ("Modifié", modified(&e)),
            (
                "Permissions",
                fs::metadata(&e.path)
                    .map(|m| format!("{:o}", m.permissions().mode() & 0o777))
                    .unwrap_or_else(|_| "—".into()),
            ),
        ];
        for (i, (k, v)) in rows.iter().enumerate() {
            let y = body_y + 140.0 + i as f32 * 24.0;
            label(c, details_x + 15.0, y, 2, 9.0, MUTED, k);
            label(c, details_x + 15.0, y + 13.0, 2, 10.0, SUB, v);
        }
    } else {
        unsafe {
            icon_glyph(c, 22, details_x + right_w / 2.0, body_y + 45.0, 24.0, MUTED);
        }
        label(
            c,
            details_x + 16.0,
            body_y + 83.0,
            2,
            11.0,
            SUB,
            "Sélectionnez un fichier",
        );
        label(
            c,
            details_x + 16.0,
            body_y + 100.0,
            2,
            10.0,
            MUTED,
            "pour afficher ses détails.",
        );
    }
    let actions_y = body_y + 242.0;
    label(c, details_x + 16.0, actions_y, 3, 12.0, GOLD, "Actions");
    let selected = s.selected.and_then(|i| s.items.get(i)).is_some();
    let action_items = [
        ("Ouvrir", 0),
        ("Nouvel onglet", 7),
        ("Copier", 1),
        ("Couper", 2),
        ("Coller", 3),
        ("Renommer", 4),
        ("Supprimer", 5),
        ("Nouveau dossier", 6),
    ];
    for (i, (name, act)) in action_items.iter().enumerate() {
        let y = actions_y + 14.0 + i as f32 * 32.0;
        let enabled = match act {
            0 | 1 | 2 | 4 | 5 | 7 => selected,
            3 => !s.clipboard.is_empty(),
            _ => true,
        };
        let col = if !enabled { MUTED } else { TXT };
        if unsafe { ui_hover(details_x + 8.0, y, right_w - 16.0, 29.0) } {
            unsafe {
                g_rrect(c, details_x + 8.0, y, right_w - 16.0, 29.0, 4.0, 4.0, HOVER);
            }
        }
        let glyph = match act {
            0 => 22,
            1 => 47,
            2 => 47,
            3 => 48,
            4 => 20,
            5 => 19,
            _ => 18,
        };
        unsafe {
            icon_glyph(c, glyph, details_x + 24.0, y + 14.0, 14.0, col);
        }
        label(c, details_x + 42.0, y + 18.0, 2, 10.0, col, name);
        if enabled && unsafe { ui_click(details_x + 8.0, y, right_w - 16.0, 29.0) } {
            match act {
                0 => open_selected(&mut s),
                7 => {
                    if let Some(e) = s.selected.and_then(|i| s.items.get(i)).cloned() {
                        let target = if e.is_dir { e.path } else { s.path.clone() };
                        s.open_tab(target);
                    }
                }
                1 | 2 => {
                    if let Some(p) = s.selected_path() {
                        s.clipboard = vec![p.to_path_buf()];
                        s.cut = *act == 2;
                        s.status = if s.cut {
                            "Élément prêt à déplacer.".into()
                        } else {
                            "Élément copié.".into()
                        };
                    }
                }
                3 => paste(&mut s),
                4 => {
                    if let Some(i) = s.selected {
                        let name = s.items[i].name.clone();
                        write_buf(&mut s.modal_entry, &name);
                        s.modal = Modal::Rename;
                    }
                }
                5 => s.modal = Modal::Delete,
                _ => {
                    write_buf(&mut s.modal_entry, "Nouveau dossier");
                    s.modal = Modal::NewFolder;
                }
            }
        }
    }
    let stat = if s.status.is_empty() {
        format!("{} éléments · {}", matches.len(), s.path.display())
    } else {
        s.status.clone()
    };
    label(c, list_x + 8.0, h - 11.0, 2, 9.0, MUTED, &stat);
    let hidden = if s.show_hidden {
        "Afficher les fichiers cachés"
    } else {
        "Fichiers cachés masqués"
    };
    if button(c, list_x + list_w - 157.0, h - 29.0, 148.0, 23.0, hidden, 2) {
        s.show_hidden = !s.show_hidden;
        s.reload();
    }
    if s.modal != Modal::None {
        draw_modal(c, &mut s, w, h);
    }
    if unsafe { U.ev == 6 && U.focus == 0 && s.modal == Modal::None } {
        match unsafe { U.key } {
            0x110003 if !s.items.is_empty() => {
                s.selected = Some(
                    (s.selected.unwrap_or(usize::MAX).saturating_add(1)).min(s.items.len() - 1),
                )
            }
            0x110002 if !s.items.is_empty() => {
                s.selected = Some(s.selected.unwrap_or(0).saturating_sub(1))
            }
            10 => open_selected(&mut s),
            8 if s.path != Path::new("/") => {
                let up = parent(&s.path);
                s.navigate(up, true);
            }
            0x11000d => s.reload(),
            27 => s.modal = Modal::None,
            _ => {}
        }
    }
}
fn draw_modal(c: *mut Canvas, s: &mut State, w: f32, h: f32) {
    unsafe {
        g_rect(c, 0.0, 0.0, w, h, 0x99000000);
    }
    let mw = 390.0;
    let mh = 190.0;
    let x = (w - mw) / 2.0;
    let y = (h - mh) / 2.0;
    unsafe {
        g_rrect(c, x, y, mw, mh, 9.0, 9.0, PANEL);
        g_rrect_line(c, x, y, mw, mh, 9.0, 1.0, BORDER);
    }
    let title = match s.modal {
        Modal::NewFolder => "Créer un dossier",
        Modal::Rename => "Renommer",
        Modal::Delete => "Confirmer la suppression",
        Modal::None => "",
    };
    label(c, x + 20.0, y + 32.0, 3, 16.0, TXT, title);
    if s.modal == Modal::Delete {
        let name = s
            .selected
            .and_then(|i| s.items.get(i))
            .map(|e| e.name.as_str())
            .unwrap_or("");
        label(
            c,
            x + 20.0,
            y + 67.0,
            2,
            11.0,
            SUB,
            &format!("Supprimer définitivement « {name} » ?"),
        );
        label(
            c,
            x + 20.0,
            y + 87.0,
            2,
            10.0,
            MUTED,
            "Les liens symboliques ne seront pas suivis.",
        );
    } else {
        let ph = cstr("Nom");
        let _ = unsafe {
            ui_field(
                8,
                s.modal_entry.as_mut_ptr(),
                s.modal_entry.len() as i32,
                x + 20.0,
                y + 56.0,
                mw - 40.0,
                34.0,
                ph.as_ptr(),
            )
        };
    }
    if button(
        c,
        x + mw - 226.0,
        y + mh - 47.0,
        96.0,
        30.0,
        if s.modal == Modal::Delete {
            "Supprimer"
        } else {
            "Valider"
        },
        1,
    ) {
        match s.modal {
            Modal::NewFolder => {
                let name = read_buf(&s.modal_entry);
                if valid_name(&name) {
                    let dest = s.path.join(&name);
                    match fs::create_dir(&dest) {
                        Ok(()) => s.status = format!("Dossier créé : {name}"),
                        Err(e) => s.status = format!("Création impossible : {e}"),
                    }
                } else {
                    s.status = "Nom invalide ou déjà utilisé.".into();
                }
                s.reload();
                s.modal = Modal::None;
            }
            Modal::Rename => {
                let name = read_buf(&s.modal_entry);
                if !valid_name(&name) {
                    s.status = "Nom invalide ou déjà utilisé.".into();
                } else if let Some(i) = s.selected {
                    let old = s.items[i].path.clone();
                    let new = s.path.join(&name);
                    match fs::rename(old, new) {
                        Ok(()) => s.status = format!("Renommé en {name}"),
                        Err(e) => s.status = format!("Renommage impossible : {e}"),
                    };
                    s.reload();
                }
                s.modal = Modal::None;
            }
            Modal::Delete => {
                if let Some(path) = s.selected_path().map(Path::to_path_buf) {
                    match delete_tree(&path) {
                        Ok(()) => s.status = "Élément supprimé.".into(),
                        Err(e) => s.status = format!("Suppression impossible : {e}"),
                    };
                    s.reload();
                }
                s.modal = Modal::None;
            }
            Modal::None => {}
        }
    }
    if button(c, x + mw - 116.0, y + mh - 47.0, 96.0, 30.0, "Annuler", 0) {
        s.modal = Modal::None;
    }
}
fn valid_name(name: &str) -> bool {
    !name.is_empty()
        && name != "."
        && name != ".."
        && !name.contains('/')
        && !name.contains('\0')
        && !name.as_bytes().contains(&0)
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::time::{SystemTime, UNIX_EPOCH};
    fn fixture() -> PathBuf {
        let n = SystemTime::now()
            .duration_since(UNIX_EPOCH)
            .unwrap()
            .as_nanos();
        let p = std::env::temp_dir().join(format!("flx-explorer-{}-{n}", std::process::id()));
        fs::create_dir(&p).unwrap();
        p
    }
    #[test]
    fn validates_names_and_classifies_common_types() {
        assert!(valid_name("rapport final.pdf"));
        assert!(!valid_name(""));
        assert!(!valid_name("../outside"));
        assert!(!valid_name("."));
        assert_eq!(
            kind_label(&Entry {
                path: PathBuf::from("x.PDF"),
                name: "x.PDF".into(),
                is_dir: false,
                is_symlink: false,
                size: 1,
                modified: None
            }),
            "PDF"
        );
    }
    #[test]
    fn copies_nested_files_and_refuses_overwrite() {
        let root = fixture();
        let src = root.join("src");
        let dst = root.join("dst");
        fs::create_dir(&src).unwrap();
        fs::create_dir(src.join("nested")).unwrap();
        fs::write(src.join("nested/data.txt"), b"FLX").unwrap();
        copy_tree(&src, &dst).unwrap();
        assert_eq!(fs::read(dst.join("nested/data.txt")).unwrap(), b"FLX");
        assert!(copy_tree(&src, &dst).is_err());
        fs::remove_dir_all(root).unwrap();
    }
    #[cfg(unix)]
    #[test]
    fn deleting_symlink_does_not_delete_target() {
        use std::os::unix::fs::symlink;
        let root = fixture();
        let target = root.join("target");
        let link = root.join("link");
        fs::write(&target, b"preserve").unwrap();
        symlink(&target, &link).unwrap();
        delete_tree(&link).unwrap();
        assert!(target.exists());
        assert!(!link.exists());
        fs::remove_dir_all(root).unwrap();
    }
}
fn main() {
    let mut args: Vec<CString> = std::env::args()
        .map(|s| CString::new(s).unwrap_or_default())
        .collect();
    let mut argv: Vec<*mut i8> = args.iter_mut().map(|s| s.as_ptr() as *mut i8).collect();
    let mut app = AppDef {
        exe: b"explorer\0".as_ptr() as _,
        title: b"Explorateur de fichiers\0".as_ptr() as _,
        w: 1360,
        h: 800,
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
        std::process::exit(app_main(&mut app, argv.len() as i32, argv.as_mut_ptr()));
    }
}
