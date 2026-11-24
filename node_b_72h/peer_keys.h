// peer_keys.h - each node's ATECC608A PUBLIC key (safe to share; it cannot sign anything).
// Run atecc_setup on each board, then paste the printed arrays here. Same file goes on both nodes.
#pragma once
#include <stdint.h>
static const uint8_t PUBKEY_NODE_A[64] = { 0 };   // <- paste Node A's key
static const uint8_t PUBKEY_NODE_B[64] = { 0 };   // <- paste Node B's key
