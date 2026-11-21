use std::{env, fs, ptr};

fn main() -> Result<(), Box<dyn std::error::Error>> {
    let args: Vec<String> = env::args().collect();
    if args.len() != 3 { return Err("usage: icax-sparrow-run REQUEST.json RESPONSE.json".into()); }
    let text = fs::read_to_string(&args[1])?;
    let response = icax_sparrow_backend::solve_json(&text, None, ptr::null_mut());
    fs::write(&args[2], response)?;
    Ok(())
}
