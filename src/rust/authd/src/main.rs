use std::{
    fs::{self, File, OpenOptions},
    io::{BufRead, BufReader, Read, Write},
    os::unix::{
        fs::{MetadataExt, OpenOptionsExt, PermissionsExt},
        net::{UnixListener, UnixStream},
    },
    path::Path,
    time::{Duration, Instant},
};

const SOCKET: &str = "/run/flx-auth.sock";
const DB: &str = "/home/.flx-system/auth.db";
const USERS: &str = "/home/.flx-system/users.db";
const MAX_USERS: usize = 8;
const ITERATIONS: u32 = 180_000;
const MIN_PASSWORD: usize = 1;

unsafe extern "C" {
    fn flx_pbkdf2_sha256(
        password: *const u8,
        password_len: usize,
        salt: *const u8,
        salt_len: usize,
        iterations: u32,
        out: *mut u8,
    ) -> i32;
    fn geteuid() -> u32;
    fn umask(mask: u32) -> u32;
    fn chown(path: *const i8, owner: u32, group: u32) -> i32;
}

#[derive(Clone)]
struct Record {
    iterations: u32,
    salt: [u8; 16],
    hash: [u8; 32],
    recovery: Option<([u8; 16], [u8; 32])>,
}
#[derive(Clone, Debug)]
struct User {
    name: String,
    uid: u32,
    gid: u32,
    role: String,
    fullname: String,
}
fn valid_name(s: &str) -> bool {
    !s.is_empty()
        && s.len() <= 31
        && s.bytes().enumerate().all(|(i, b)| {
            b.is_ascii_lowercase()
                || b.is_ascii_digit()
                || (i == 0 && b == b'_')
                || (i > 0 && b == b'-')
        })
        && s.as_bytes()[0].is_ascii_lowercase()
}
fn user_db(name: &str) -> Option<String> {
    if !valid_name(name) {
        return None;
    }
    Some(if name == "flx" {
        DB.to_string()
    } else {
        format!("/home/.flx-system/auth-{name}.db")
    })
}
fn secure_root_dir(path: &str, mode: u32) -> std::io::Result<()> {
    fs::create_dir_all(path)?;
    let meta = fs::symlink_metadata(path)?;
    if !meta.is_dir() || meta.file_type().is_symlink() {
        return Err(std::io::Error::new(
            std::io::ErrorKind::PermissionDenied,
            "unsafe service directory",
        ));
    }
    let c = std::ffi::CString::new(path).unwrap();
    if unsafe { chown(c.as_ptr(), 0, 0) } != 0 {
        return Err(std::io::Error::last_os_error());
    }
    fs::set_permissions(path, fs::Permissions::from_mode(mode))
}
fn secure_record(path: &str, allow_missing: bool) -> std::io::Result<()> {
    let meta = match fs::symlink_metadata(path) {
        Ok(meta) => meta,
        Err(e) if e.kind() == std::io::ErrorKind::NotFound && allow_missing => return Ok(()),
        Err(e) => return Err(e),
    };
    if !meta.is_file() || meta.file_type().is_symlink() || meta.uid() != 0 {
        return Err(std::io::Error::new(
            std::io::ErrorKind::PermissionDenied,
            "unsafe credential record",
        ));
    }
    fs::set_permissions(path, fs::Permissions::from_mode(0o600))
}
fn load_users() -> Vec<User> {
    let Ok(text) = fs::read_to_string(USERS) else {
        return Vec::new();
    };
    let mut lines = text.lines();
    if lines.next() != Some("FLXUSERS1") {
        return Vec::new();
    }
    lines
        .filter_map(|line| {
            let mut p = line.splitn(5, ':');
            let name = p.next()?.to_string();
            let uid = p.next()?.parse().ok()?;
            let gid = p.next()?.parse().ok()?;
            let role = p.next()?.to_string();
            let fullname = String::from_utf8(decode_hex(p.next()?)?).ok()?;
            if !valid_name(&name)
                || uid < 1000
                || gid < 1000
                || (role != "admin" && role != "standard")
            {
                return None;
            }
            Some(User {
                name,
                uid,
                gid,
                role,
                fullname,
            })
        })
        .take(MAX_USERS)
        .collect()
}
fn users_db_valid() -> bool {
    let Ok(text) = fs::read_to_string(USERS) else {
        return false;
    };
    let mut lines = text.lines();
    if lines.next() != Some("FLXUSERS1") {
        return false;
    }
    let records: Vec<&str> = lines.collect();
    if records.is_empty() || records.len() > MAX_USERS {
        return false;
    }
    let users = load_users();
    if users.len() != records.len() {
        return false;
    }
    for (i, user) in users.iter().enumerate() {
        if users[..i]
            .iter()
            .any(|other| other.name == user.name || other.uid == user.uid || other.gid == user.gid)
        {
            return false;
        }
    }
    users
        .iter()
        .any(|u| u.name == "flx" && u.uid == 1000 && u.gid == 1000 && u.role == "admin")
}
fn save_users(users: &[User]) -> std::io::Result<()> {
    let temp = "/home/.flx-system/.users.tmp";
    let mut f = OpenOptions::new()
        .write(true)
        .create(true)
        .truncate(true)
        .mode(0o600)
        .open(temp)?;
    writeln!(f, "FLXUSERS1")?;
    for u in users {
        writeln!(
            f,
            "{}:{}:{}:{}:{}",
            u.name,
            u.uid,
            u.gid,
            u.role,
            encode(u.fullname.as_bytes())
        )?;
    }
    f.sync_all()?;
    fs::rename(temp, USERS)?;
    File::open("/home/.flx-system")?.sync_all()
}
fn write_passwd(users: &[User]) -> std::io::Result<()> {
    let mut passwd = String::from("root:x:0:0:root:/root:/bin/sh\n");
    let mut group = String::from("root:x:0:\n");
    for u in users {
        passwd.push_str(&format!(
            "{}:x:{}:{}:{}:/home/users/{}:/bin/sh\n",
            u.name,
            u.uid,
            u.gid,
            u.fullname.replace([':', '\n'], " "),
            u.name
        ));
        group.push_str(&format!("{}:x:{}:\n", u.name, u.gid));
    }
    fs::write("/etc/passwd.flx", passwd)?;
    fs::set_permissions("/etc/passwd.flx", fs::Permissions::from_mode(0o644))?;
    fs::rename("/etc/passwd.flx", "/etc/passwd")?;
    fs::write("/etc/group.flx", group)?;
    fs::set_permissions("/etc/group.flx", fs::Permissions::from_mode(0o644))?;
    fs::rename("/etc/group.flx", "/etc/group")
}
fn account_home(name: &str, uid: u32, gid: u32) -> std::io::Result<()> {
    let path = format!("/home/users/{name}");
    match fs::symlink_metadata(&path) {
        Ok(meta) => {
            if !meta.is_dir()
                || meta.file_type().is_symlink()
                || meta.uid() != uid
                || meta.mode() & 0o777 != 0o700
            {
                return Err(std::io::Error::new(
                    std::io::ErrorKind::PermissionDenied,
                    "unsafe or incorrectly owned account home",
                ));
            }
            // Existing homes belong to a logged-in user. Never walk or chmod
            // their contents as root; that would follow attacker-planted links.
            return Ok(());
        }
        Err(e) if e.kind() == std::io::ErrorKind::NotFound => {}
        Err(e) => return Err(e),
    }
    fs::create_dir(&path)?;
    fs::set_permissions(&path, fs::Permissions::from_mode(0o700))?;
    let c = std::ffi::CString::new(path.as_str()).unwrap();
    if unsafe { chown(c.as_ptr(), uid, gid) } != 0 {
        return Err(std::io::Error::last_os_error());
    }
    for part in [
        "Documents",
        "Pictures",
        "Desktop",
        "Downloads",
        "Music",
        "Videos",
        ".config",
    ] {
        let child = format!("{path}/{part}");
        fs::create_dir_all(&child)?;
        let mode = if part.starts_with('.') { 0o700 } else { 0o755 };
        fs::set_permissions(&child, fs::Permissions::from_mode(mode))?;
        let c = std::ffi::CString::new(child).unwrap();
        if unsafe { chown(c.as_ptr(), uid, gid) } != 0 {
            return Err(std::io::Error::last_os_error());
        }
    }
    Ok(())
}
fn derive(password: &[u8], salt: &[u8], rounds: u32) -> Option<[u8; 32]> {
    let mut out = [0u8; 32];
    let ok = unsafe {
        flx_pbkdf2_sha256(
            password.as_ptr(),
            password.len(),
            salt.as_ptr(),
            salt.len(),
            rounds,
            out.as_mut_ptr(),
        )
    };
    (ok == 1).then_some(out)
}
fn encode(bytes: &[u8]) -> String {
    bytes.iter().map(|b| format!("{b:02x}")).collect()
}
fn decode<const N: usize>(s: &str) -> Option<[u8; N]> {
    if s.len() != N * 2 {
        return None;
    }
    let mut out = [0u8; N];
    for (i, b) in out.iter_mut().enumerate() {
        *b = u8::from_str_radix(&s[i * 2..i * 2 + 2], 16).ok()?;
    }
    Some(out)
}
fn decode_hex(s: &str) -> Option<Vec<u8>> {
    if s.len() % 2 != 0 {
        return None;
    }
    (0..s.len())
        .step_by(2)
        .map(|i| u8::from_str_radix(&s[i..i + 2], 16).ok())
        .collect()
}
fn random<const N: usize>() -> Option<[u8; N]> {
    let mut out = [0u8; N];
    File::open("/dev/urandom").ok()?.read_exact(&mut out).ok()?;
    Some(out)
}
fn read_record(path: &str) -> Option<Record> {
    let text = fs::read_to_string(path).ok()?;
    let mut lines = text.lines();
    let version = lines.next()?;
    if version != "FLXAUTH1" && version != "FLXAUTH2" {
        return None;
    }
    let iterations = lines.next()?.parse::<u32>().ok()?;
    if !(10_000..=2_000_000).contains(&iterations) {
        return None;
    }
    let salt = decode(lines.next()?)?;
    let hash = decode(lines.next()?)?;
    let recovery = if version == "FLXAUTH2" {
        match (lines.next()?, lines.next()?) {
            ("-", "-") => None,
            (s, h) => Some((decode(s)?, decode(h)?)),
        }
    } else {
        None
    };
    Some(Record {
        iterations,
        salt,
        hash,
        recovery,
    })
}
fn persist(path: &str, record: &Record) -> std::io::Result<()> {
    let temp = format!("{path}.tmp");
    let _ = fs::remove_file(&temp);
    let mut f = OpenOptions::new()
        .write(true)
        .create_new(true)
        .mode(0o600)
        .open(&temp)?;
    writeln!(
        f,
        "FLXAUTH2\n{}\n{}\n{}",
        record.iterations,
        encode(&record.salt),
        encode(&record.hash)
    )?;
    if let Some((salt, hash)) = record.recovery {
        writeln!(f, "{}\n{}", encode(&salt), encode(&hash))?;
    } else {
        writeln!(f, "-\n-")?;
    }
    f.sync_all()?;
    fs::rename(&temp, path)?;
    File::open("/home/.flx-system")?.sync_all()?;
    Ok(())
}
fn make_recovery(record: &mut Record) -> Option<String> {
    let token = random::<16>()?;
    let salt = random::<16>()?;
    let token_text = encode(&token);
    let hash = derive(token_text.as_bytes(), &salt, ITERATIONS)?;
    record.recovery = Some((salt, hash));
    Some(token_text)
}
fn password_matches(record: &Record, password: &[u8]) -> bool {
    if !valid_password(password) {
        return false;
    }
    derive(password, &record.salt, record.iterations)
        .map(|mut actual| {
            let ok = constant_eq(&actual, &record.hash);
            actual.fill(0);
            ok
        })
        .unwrap_or(false)
}
fn recovery_matches(record: &Record, token: &[u8]) -> bool {
    if token.len() != 32 {
        return false;
    }
    let Some((salt, expected)) = record.recovery else {
        return false;
    };
    derive(token, &salt, record.iterations)
        .map(|mut actual| {
            let ok = constant_eq(&actual, &expected);
            actual.fill(0);
            ok
        })
        .unwrap_or(false)
}
fn constant_eq(a: &[u8], b: &[u8]) -> bool {
    if a.len() != b.len() {
        return false;
    }
    a.iter().zip(b).fold(0u8, |d, (x, y)| d | (x ^ y)) == 0
}
fn valid_password(password: &[u8]) -> bool {
    (MIN_PASSWORD..=256).contains(&password.len())
}
fn wipe_string(value: &mut String) {
    unsafe {
        value.as_bytes_mut().fill(0);
    }
}
fn reply(stream: &mut UnixStream, value: &str) {
    let _ = writeln!(stream, "{value}");
}
fn new_password(path: &str, record: &mut Record, password: &[u8]) -> bool {
    if !valid_password(password) {
        return false;
    }
    let Some(salt) = random::<16>() else {
        return false;
    };
    let Some(mut hash) = derive(password, &salt, ITERATIONS) else {
        return false;
    };
    record.salt = salt;
    record.hash = hash;
    hash.fill(0);
    persist(path, record).is_ok()
}
fn setup(path: &str, password: &[u8]) -> bool {
    if !valid_password(password) || Path::new(path).exists() {
        return false;
    }
    let Some(salt) = random::<16>() else {
        return false;
    };
    let Some(hash) = derive(password, &salt, ITERATIONS) else {
        return false;
    };
    let mut r = Record {
        iterations: ITERATIONS,
        salt,
        hash,
        recovery: None,
    };
    let result = make_recovery(&mut r).is_some() && persist(path, &r).is_ok();
    r.hash.fill(0);
    result
}
fn verify_user(name: &str, password: &[u8]) -> Option<User> {
    if !valid_password(password) {
        return None;
    }
    let user = load_users().into_iter().find(|u| u.name == name)?;
    let path = user_db(name)?;
    if Path::new(&path).exists() {
        let mut record = read_record(&path)?;
        let ok = password_matches(&record, password);
        record.hash.fill(0);
        if !ok {
            return None;
        }
    } else if !password.is_empty() {
        return None;
    }
    Some(user)
}
fn user_list() -> String {
    let users = load_users();
    let mut out = String::from("USERS");
    for u in users {
        out.push(' ');
        out.push_str(&format!(
            "{}:{}:{}:{}:{}",
            u.name,
            u.uid,
            u.gid,
            u.role,
            encode(u.fullname.as_bytes())
        ));
    }
    out
}
fn add_user(
    actor: &str,
    actor_password: &[u8],
    name: &str,
    fullname_hex: &str,
    role: &str,
    password: &[u8],
) -> bool {
    let mut users = load_users();
    let Some(admin) = users.iter().find(|u| u.name == actor && u.role == "admin") else {
        return false;
    };
    let _ = admin;
    let Some(actor_path) = user_db(actor) else {
        return false;
    };
    if actor_password.is_empty()
        || !Path::new(&actor_path).exists()
        || verify_user(actor, actor_password).is_none()
        || users.len() >= MAX_USERS
        || !valid_name(name)
        || users.iter().any(|u| u.name == name)
        || (role != "admin" && role != "standard")
        || !valid_password(password)
    {
        return false;
    }
    let Some(fullname) = decode_hex(fullname_hex).and_then(|v| String::from_utf8(v).ok()) else {
        return false;
    };
    if fullname.is_empty()
        || fullname.len() > 96
        || fullname.chars().any(|c| c.is_control() || c == ':')
    {
        return false;
    }
    let uid = users
        .iter()
        .map(|u| u.uid)
        .max()
        .unwrap_or(999)
        .saturating_add(1);
    if uid > 60_000 {
        return false;
    }
    let user = User {
        name: name.to_string(),
        uid,
        gid: uid,
        role: role.to_string(),
        fullname,
    };
    if account_home(name, uid, uid).is_err() {
        return false;
    }
    let Some(path) = user_db(name) else {
        return false;
    };
    let Some(salt) = random::<16>() else {
        return false;
    };
    let Some(hash) = derive(password, &salt, ITERATIONS) else {
        return false;
    };
    let mut record = Record {
        iterations: ITERATIONS,
        salt,
        hash,
        recovery: None,
    };
    let stored = persist(&path, &record).is_ok();
    record.hash.fill(0);
    if !stored {
        return false;
    }
    users.push(user);
    if save_users(&users).is_err() {
        let _ = fs::remove_file(path);
        return false;
    }
    write_passwd(&users).is_ok()
}
fn handle(stream: &mut UnixStream, failures: &mut u32, blocked_until: &mut Instant) {
    let _ = stream.set_read_timeout(Some(Duration::from_secs(5)));
    let mut line = String::new();
    if BufReader::new(stream.try_clone().unwrap())
        .take(4096)
        .read_line(&mut line)
        .is_err()
    {
        wipe_string(&mut line);
        reply(stream, "ERROR");
        return;
    }
    while line.ends_with('\r') || line.ends_with('\n') {
        line.pop();
    }
    let parts: Vec<&str> = line.split_whitespace().collect();
    let cmd = parts.first().copied().unwrap_or("");
    if cmd == "LIST" {
        reply(stream, &user_list());
        return;
    }
    if cmd == "STATUS" {
        let name = parts.get(1).copied().unwrap_or("flx");
        let response = if load_users().iter().any(|u| u.name == name) {
            match user_db(name) {
                Some(path) if Path::new(&path).exists() => "ENROLLED",
                Some(_) => "EMPTY",
                None => "ERROR",
            }
        } else {
            "ERROR"
        };
        reply(stream, response);
        return;
    }
    if Instant::now() < *blocked_until {
        reply(stream, "DENIED");
        return;
    }
    let mut response = "DENIED".to_string();
    match cmd {
        "AUTH" | "VERIFY" if parts.len() == 3 => {
            if let Some(mut pass) = decode_hex(parts[2]) {
                if let Some(user) = verify_user(parts[1], &pass) {
                    response = if cmd == "AUTH" {
                        format!("OK {} {} {}", user.uid, user.gid, user.role)
                    } else {
                        "OK".into()
                    };
                }
                pass.fill(0);
            }
        }
        "SETHEX" if parts.len() == 3 => {
            if let (Some(path), Some(mut password)) = (user_db(parts[1]), decode_hex(parts[2])) {
                if load_users().iter().any(|u| u.name == parts[1]) && setup(&path, &password) {
                    response = "OK".into();
                }
                password.fill(0);
            }
        }
        "SETHEX" if parts.len() == 2 => {
            if let (Some(path), Some(mut password)) = (user_db("flx"), decode_hex(parts[1])) {
                if setup(&path, &password) {
                    response = "OK".into();
                }
                password.fill(0);
            }
        }
        "CHANGE" if parts.len() == 4 => {
            if let (Some(path), Some(mut old), Some(mut new)) = (
                user_db(parts[1]),
                decode_hex(parts[2]),
                decode_hex(parts[3]),
            ) {
                if let Some(mut r) = read_record(&path) {
                    if password_matches(&r, &old) && new_password(&path, &mut r, &new) {
                        response = "OK".into();
                    }
                    r.hash.fill(0);
                }
                old.fill(0);
                new.fill(0);
            }
        }
        "CHANGE" if parts.len() == 3 => {
            if let (Some(path), Some(mut old), Some(mut new)) =
                (user_db("flx"), decode_hex(parts[1]), decode_hex(parts[2]))
            {
                if let Some(mut r) = read_record(&path) {
                    if password_matches(&r, &old) && new_password(&path, &mut r, &new) {
                        response = "OK".into();
                    }
                    r.hash.fill(0);
                }
                old.fill(0);
                new.fill(0);
            }
        }
        "ROTATE" if parts.len() == 3 => {
            if let (Some(path), Some(mut old)) = (user_db(parts[1]), decode_hex(parts[2])) {
                if let Some(mut r) = read_record(&path) {
                    if password_matches(&r, &old) {
                        if let Some(token) = make_recovery(&mut r) {
                            if persist(&path, &r).is_ok() {
                                response = format!("RECOVERY {token}");
                            }
                        }
                    }
                    r.hash.fill(0);
                }
                old.fill(0);
            }
        }
        "ROTATE" if parts.len() == 2 => {
            if let (Some(path), Some(mut old)) = (user_db("flx"), decode_hex(parts[1])) {
                if let Some(mut r) = read_record(&path) {
                    if password_matches(&r, &old) {
                        if let Some(token) = make_recovery(&mut r) {
                            if persist(&path, &r).is_ok() {
                                response = format!("RECOVERY {token}");
                            }
                        }
                    }
                    r.hash.fill(0);
                }
                old.fill(0);
            }
        }
        "RECOVER" if parts.len() == 4 => {
            if let (Some(path), Some(mut token), Some(mut new)) = (
                user_db(parts[1]),
                decode_hex(parts[2]),
                decode_hex(parts[3]),
            ) {
                if let Some(mut r) = read_record(&path) {
                    if recovery_matches(&r, &token) {
                        r.recovery = None;
                        if new_password(&path, &mut r, &new) {
                            response = "OK".into();
                        }
                    }
                    r.hash.fill(0);
                }
                token.fill(0);
                new.fill(0);
            }
        }
        "RECOVER" if parts.len() == 3 => {
            if let (Some(path), Some(mut token), Some(mut new)) =
                (user_db("flx"), decode_hex(parts[1]), decode_hex(parts[2]))
            {
                if let Some(mut r) = read_record(&path) {
                    if recovery_matches(&r, &token) {
                        r.recovery = None;
                        if new_password(&path, &mut r, &new) {
                            response = "OK".into();
                        }
                    }
                    r.hash.fill(0);
                }
                token.fill(0);
                new.fill(0);
            }
        }
        "ADD" if parts.len() == 7 => {
            if let (Some(mut admin_pass), Some(mut new_pass)) =
                (decode_hex(parts[2]), decode_hex(parts[6]))
            {
                if add_user(
                    parts[1],
                    &admin_pass,
                    parts[3],
                    parts[4],
                    parts[5],
                    &new_pass,
                ) {
                    response = "OK".into();
                }
                admin_pass.fill(0);
                new_pass.fill(0);
            }
        }
        _ => {}
    }
    if response == "OK" || response.starts_with("OK ") || response.starts_with("RECOVERY ") {
        *failures = 0;
    } else {
        *failures = failures.saturating_add(1);
        if *failures >= 5 {
            *blocked_until = Instant::now() + Duration::from_secs(20);
            *failures = 0;
        }
    }
    drop(parts);
    wipe_string(&mut line);
    reply(stream, &response);
}

fn main() -> std::io::Result<()> {
    if unsafe { geteuid() } != 0 {
        return Err(std::io::Error::new(
            std::io::ErrorKind::PermissionDenied,
            "flx-authd must run as root",
        ));
    }
    unsafe {
        umask(0o077);
    }
    secure_root_dir("/home/.flx-system", 0o700)?;
    secure_root_dir("/home/users", 0o755)?;
    secure_record(USERS, true)?;
    let mut users = load_users();
    if Path::new(USERS).exists() && !users_db_valid() {
        return Err(std::io::Error::new(
            std::io::ErrorKind::InvalidData,
            "user database is malformed; refusing to reset identities",
        ));
    }
    if !users.iter().any(|u| u.name == "flx") {
        if users.len() >= MAX_USERS {
            return Err(std::io::Error::other(
                "user database has no room for the initial account",
            ));
        }
        users.insert(
            0,
            User {
                name: "flx".into(),
                uid: 1000,
                gid: 1000,
                role: "admin".into(),
                fullname: "FLX Desktop".into(),
            },
        );
        save_users(&users)?;
    }
    for u in &users {
        account_home(&u.name, u.uid, u.gid)?;
        if u.name != "flx" {
            let path = user_db(&u.name).unwrap();
            let meta = fs::symlink_metadata(&path)?;
            if !meta.is_file()
                || meta.file_type().is_symlink()
                || meta.uid() != 0
                || meta.mode() & 0o777 != 0o600
                || read_record(&path).is_none()
            {
                return Err(std::io::Error::new(
                    std::io::ErrorKind::PermissionDenied,
                    "account credential record is missing or unsafe",
                ));
            }
        }
    }
    write_passwd(&users)?;
    // Preserve credentials from older builds, but move them immediately out
    // of the historically shared config directory into the protected area.
    let legacy = "/home/.config/flx-auth.db";
    if !Path::new(DB).exists() && Path::new(legacy).exists() {
        fs::rename(legacy, DB)?;
        let c = std::ffi::CString::new(DB).unwrap();
        if unsafe { chown(c.as_ptr(), 0, 0) } != 0 {
            return Err(std::io::Error::last_os_error());
        }
    }
    for user in &users {
        if let Some(path) = user_db(&user.name) {
            secure_record(&path, user.name == "flx")?;
        }
    }
    let _ = fs::remove_file(SOCKET);
    let listener = UnixListener::bind(SOCKET)?;
    fs::set_permissions(SOCKET, fs::Permissions::from_mode(0o600))?;
    let mut failures = 0;
    let mut blocked_until = Instant::now();
    for stream in listener.incoming() {
        if let Ok(mut stream) = stream {
            handle(&mut stream, &mut failures, &mut blocked_until);
        }
    }
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn one_character_password_is_valid_and_empty_is_rejected() {
        assert!(valid_password(b"a"));
        assert!(valid_password("é".as_bytes()));
        assert!(!valid_password(b""));
        assert!(!valid_password(&[b'x'; 257]));
    }

    #[test]
    fn hex_parser_rejects_malformed_or_unbounded_values() {
        assert_eq!(decode_hex("61"), Some(vec![b'a']));
        assert!(decode_hex("6").is_none());
        assert!(decode_hex("gg").is_none());
        for _ in 0..10_000 {
            let _ = decode_hex("00112233445566778899aabbccddeeff");
        }
    }

    #[test]
    fn constant_time_comparison_checks_full_values() {
        assert!(constant_eq(b"credential", b"credential"));
        assert!(!constant_eq(b"credential", b"credentiaL"));
        assert!(!constant_eq(b"short", b"longer"));
    }
}
