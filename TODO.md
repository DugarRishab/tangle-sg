# A list of To-Do to make tangle_SG better

## FEATURE 1: Dual signature Rule

This is to allow both the sender and the receiver to sign off on the transaction before it is validated.

### 0. Transaction Generation

- [ ] Sender sends a half-signed Tx. `sig_sender` is signed on the tx body and the sender's private key. 
- [ ] Require the sender to perform a small, fixed PoW (diff=2–4) before broadcasting their half‑signed Tx. This serves as a “commit cost” to deter trivial spam of proposals

### 1. Challenge generation

- [ ] When Receiver sees the half‑signed Tx, it generates a fresh random nonce n, signs the tuple (Tx_hash, n) with its private key, and returns that signature+nonce to the sender (off‑tangle or in a tiny “approval” message).

### 2. Binding in the Tx
45
- [ ] The final Tx object embeds both (sig_sender, n, sig_receiver_on_[Tx_hash∥n]).

### 3. Validation

- [ ] Any node verifying the double‑signed Tx recomputes h = SHA3_256(Tx_body), checks sig_sender(h) and then checks sig_receiver(h ∥ n).

No other node can invent n or sig_receiver without the receiver’s key, and they can’t replay someone else’s approval on a different Tx because h would differ.