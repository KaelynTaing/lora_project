#!/usr/bin/env python3
"""Prints a random 16-byte AES key as a C array. Paste it into shared/secrets.h."""
import secrets
k = secrets.token_bytes(16)
print("static const uint8_t AES_KEY[16] = { " + ",".join(f"0x{b:02X}" for b in k) + " };")
