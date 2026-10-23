use std::{
    fs::{self, File, OpenOptions},
    io::{BufRead, BufReader, Read, Write},
    os::unix::{
        fs::{OpenOptionsExt, PermissionsExt},
        net::{UnixListener, UnixStream},
    },
    path::Path,
    time::{Duration, Instant},
};

const SOCKET: &str = "/run/flx-auth.sock";
const DB: &str = "/home/.flx-system/auth.db";
const ITERATIONS: u32 = 180_000;
const MIN_PASSWORD: usize = 10;

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
}

#[derive(Clone)]
struct Record {
    iterations: u32,
    salt: [u8; 16],
    hash: [u8; 32],
    recovery: Option<([u8; 16], [u8; 32])>,
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
fn read_record() -> Option<Record> {
    let text = fs::read_to_string(DB).ok()?;
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
fn persist(record: &Record) -> std::io::Result<()> {
    let temp = "/home/.flx-system/.auth.tmp";
    let _ = fs::remove_file(temp);
    let mut f = OpenOptions::new()
        .write(true)
        .create_new(true)
        .mode(0o600)
        .open(temp)?;
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
    fs::rename(temp, DB)?;
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
    derive(password, &record.salt, record.iterations)
        .map(|mut actual| {
            let ok = constant_eq(&actual, &record.hash);
            actual.fill(0);
            ok
        })
        .unwrap_or(false)
}
fn recovery_matches(record: &Record, token: &[u8]) -> bool {
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
fn wipe_string(value: &mut String) {
    unsafe {
        value.as_bytes_mut().fill(0);
    }
}
fn reply(stream: &mut UnixStream, value: &str) {
    let _ = writeln!(stream, "{value}");
}
fn new_password(record: &mut Record, password: &[u8]) -> bool {
    if password.len() < MIN_PASSWORD || password.len() > 256 {
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
    persist(record).is_ok()
}
fn setup(password: &str) -> bool {
    if password.len() < MIN_PASSWORD || password.len() > 256 || Path::new(DB).exists() {
        return false;
    }
    let Some(salt) = random::<16>() else {
        return false;
    };
    let Some(hash) = derive(password.as_bytes(), &salt, ITERATIONS) else {
        return false;
    };
    let mut r = Record {
        iterations: ITERATIONS,
        salt,
        hash,
        recovery: None,
    };
    let result = make_recovery(&mut r).is_some() && persist(&r).is_ok();
    r.hash.fill(0);
    result
}
fn handle(stream: &mut UnixStream, failures: &mut u32, blocked_until: &mut Instant) {
    let _ = stream.set_read_timeout(Some(Duration::from_secs(5)));
    let mut line = String::new();
    if BufReader::new(stream.try_clone().unwrap())
        .take(512)
        .read_line(&mut line)
        .is_err()
    {
        wipe_string(&mut line);
        reply(stream, "ERROR");
        return;
    }
    let len = line.trim_end_matches(['\r', '\n']).len();
    line.truncate(len);
    if line == "STATUS" {
        reply(
            stream,
            if Path::new(DB).exists() {
                "ENROLLED"
            } else {
                "EMPTY"
            },
        );
        wipe_string(&mut line);
        return;
    }
    if Instant::now() < *blocked_until {
        reply(stream, "DENIED");
        wipe_string(&mut line);
        return;
    }
    let mut parts = line.splitn(2, ' ');
    let cmd = parts.next().unwrap_or("");
    let args = parts.next().unwrap_or("");
    let (first, second) = args.split_once(' ').unwrap_or((args, ""));
    let mut response = "DENIED".to_string();
    if cmd == "SET" && !Path::new(DB).exists() {
        if setup(args) {
            response = "OK".into();
        }
    } else if cmd == "VERIFY" {
        if read_record()
            .map(|r| password_matches(&r, args.as_bytes()))
            .unwrap_or(false)
        {
            response = "OK".into();
        }
    } else if cmd == "CHANGE" && !first.is_empty() && !second.is_empty() {
        if let Some(mut r) = read_record() {
            if let (Some(old), Some(new)) = (decode_hex(first), decode_hex(second)) {
                if password_matches(&r, &old) && new_password(&mut r, &new) {
                    response = "OK".into();
                }
            }
            r.hash.fill(0);
        }
    } else if cmd == "ROTATE" && !first.is_empty() {
        if let Some(mut r) = read_record() {
            if decode_hex(first)
                .map(|p| password_matches(&r, &p))
                .unwrap_or(false)
            {
                if let Some(token) = make_recovery(&mut r) {
                    if persist(&r).is_ok() {
                        response = format!("RECOVERY {token}");
                    }
                }
            }
            r.hash.fill(0);
        }
    } else if cmd == "RECOVER" && !first.is_empty() && !second.is_empty() {
        if let Some(mut r) = read_record() {
            if let (Some(token), Some(new)) = (decode_hex(first), decode_hex(second)) {
                if recovery_matches(&r, &token) {
                    r.recovery = None;
                    if new_password(&mut r, &new) {
                        response = "OK".into();
                    }
                }
            }
            r.hash.fill(0);
        }
    }
    if response == "OK" || response.starts_with("RECOVERY ") {
        *failures = 0;
    } else {
        *failures = failures.saturating_add(1);
        if *failures >= 5 {
            *blocked_until = Instant::now() + Duration::from_secs(20);
            *failures = 0;
        }
    }
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
    fs::create_dir_all("/home/.flx-system")?;
    fs::set_permissions("/home/.flx-system", fs::Permissions::from_mode(0o700))?;
    // Preserve credentials from older builds, but move them immediately out
    // of the historically shared config directory into the protected area.
    let legacy = "/home/.config/flx-auth.db";
    if !Path::new(DB).exists() && Path::new(legacy).exists() {
        fs::rename(legacy, DB)?;
        fs::set_permissions(DB, fs::Permissions::from_mode(0o600))?;
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
