"""Explicit development test: ephemeral issuer key, never a production key."""
import argparse
from pathlib import Path
import time
from cryptography.hazmat.primitives.asymmetric import ec
import authority
from enrollment import parse_request, verify_ek_native
from production import activation_package, read_bounded

parser = argparse.ArgumentParser(description="Prepare a TEST ONLY hardware activation package")
parser.add_argument("request", type=Path)
parser.add_argument("root", type=Path)
parser.add_argument("intermediate", type=Path, nargs="+")
parser.add_argument("--verifier", type=Path, required=True)
parser.add_argument("--output-directory", type=Path, required=True)
args = parser.parse_args()
request = parse_request(read_bounded(args.request, 65536))
verify_ek_native(request.ek_area, request.certificates, [read_bounded(args.root, 16384)],
                 [read_bounded(p, 16384) for p in args.intermediate], args.verifier.resolve())
key = ec.generate_private_key(ec.SECP256R1())
certificate = authority.sign_body(key, authority.encode_body(issuer_id="hardware-test-only",
    license_id="hardware-test-only", request_id=request.digest, customer_id="hardware-test-only",
    device_public_key=request.device_public, kind=1, features=15, min_major=0, max_major=0, issued_at=int(time.time())))
args.output_directory.mkdir(exist_ok=False)
authority.exclusive_write(args.output_directory / "test-public.blob", authority.public_blob(key.public_key()))
authority.exclusive_write(args.output_directory / "test.tdact", activation_package(key, request, certificate))
print("Test activation package created after EK chain verification. Private issuer key was NOT saved.")
