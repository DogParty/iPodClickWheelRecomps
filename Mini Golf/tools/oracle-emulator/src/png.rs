//! A PNG writer, for the frame sequence [`crate::film`] records.
//!
//! Hand-rolled rather than pulled from `image` or `png`: a PNG is about forty lines of framing
//! around a zlib stream that is allowed to be *stored* — deflate's uncompressed block type. The
//! result is byte-exact and spec-conformant, about three times the size of a compressed one, and
//! costs this crate no dependency at all. `eapp-loader` has exactly one dependency (`arm7tdmi`,
//! the CPU) and the whole project's reproducibility argument rests on that staying true.
//!
//! **This is the second copy of this encoder in the tree.** `tools/ipod-gui/src/png.rs` carries an
//! equivalent one for its screenshot button. That crate is under concurrent edit by another agent,
//! so folding the two together is deliberately left as a follow-up rather than done here — the two
//! produce the same bytes for the same pixels, and a test below pins the format so the claim is
//! checkable rather than asserted. Whoever folds them should keep *this* one: it is in the crate
//! both front ends already depend on.

/// CRC-32 as PNG specifies it (IEEE 802.3, reflected, init and final xor `0xffffffff`).
fn crc32(data: &[u8]) -> u32 {
    let mut table = [0u32; 256];
    for (i, e) in table.iter_mut().enumerate() {
        let mut c = i as u32;
        for _ in 0..8 {
            c = if c & 1 != 0 { 0xedb8_8320 ^ (c >> 1) } else { c >> 1 };
        }
        *e = c;
    }
    let mut c = 0xffff_ffffu32;
    for b in data {
        c = table[((c ^ *b as u32) & 0xff) as usize] ^ (c >> 8);
    }
    c ^ 0xffff_ffff
}

/// Adler-32 — the zlib stream's own checksum. A *different* algorithm from the chunk CRC above,
/// and swapping the two produces a file that looks right in a hex dump and fails in every decoder.
fn adler32(data: &[u8]) -> u32 {
    let (mut a, mut b) = (1u32, 0u32);
    for x in data {
        a = (a + *x as u32) % 65521;
        b = (b + a) % 65521;
    }
    (b << 16) | a
}

fn chunk(out: &mut Vec<u8>, kind: &[u8; 4], body: &[u8]) {
    out.extend_from_slice(&(body.len() as u32).to_be_bytes());
    let mut with_kind = kind.to_vec();
    with_kind.extend_from_slice(body);
    out.extend_from_slice(&with_kind);
    out.extend_from_slice(&crc32(&with_kind).to_be_bytes());
}

/// `rgb` is `w * h * 3` bytes, row-major, top row first.
pub fn encode(rgb: &[u8], w: usize, h: usize) -> Vec<u8> {
    assert_eq!(rgb.len(), w * h * 3, "the pixel buffer is not {w}x{h} RGB");

    // Raw scanlines, each prefixed with filter type 0 (None). Filtering exists to help the
    // compressor; with a stored stream it would only add work and a chance to be wrong.
    let mut raw = Vec::with_capacity(h * (1 + w * 3));
    for y in 0..h {
        raw.push(0);
        raw.extend_from_slice(&rgb[y * w * 3..(y + 1) * w * 3]);
    }

    // zlib: two header bytes, deflate blocks, then the Adler sum. 0x78 0x01 is "deflate, 32 KiB
    // window, no preset dictionary, fastest"; the check bits make 0x7801 a multiple of 31.
    let mut z = vec![0x78, 0x01];
    let mut done = 0usize;
    while done < raw.len() {
        let n = (raw.len() - done).min(0xffff);
        z.push(u8::from(done + n == raw.len())); // BFINAL on the last block, BTYPE 00 (stored)
        z.extend_from_slice(&(n as u16).to_le_bytes());
        z.extend_from_slice(&(!(n as u16)).to_le_bytes());
        z.extend_from_slice(&raw[done..done + n]);
        done += n;
    }
    z.extend_from_slice(&adler32(&raw).to_be_bytes());

    let mut out = vec![0x89, b'P', b'N', b'G', 0x0d, 0x0a, 0x1a, 0x0a];
    let mut ihdr = Vec::new();
    ihdr.extend_from_slice(&(w as u32).to_be_bytes());
    ihdr.extend_from_slice(&(h as u32).to_be_bytes());
    ihdr.extend_from_slice(&[8, 2, 0, 0, 0]); // 8 bits, colour type 2 (truecolour), no interlace
    chunk(&mut out, b"IHDR", &ihdr);
    chunk(&mut out, b"IDAT", &z);
    chunk(&mut out, b"IEND", &[]);
    out
}

#[cfg(test)]
mod tests {
    use super::*;

    /// The published vectors for both checksums. Each is short enough to get subtly wrong and
    /// produce a file that only fails in somebody else's decoder.
    #[test]
    fn the_checksums_match_their_published_vectors() {
        assert_eq!(crc32(b"123456789"), 0xcbf4_3926);
        assert_eq!(adler32(b"Wikipedia"), 0x11e6_0398);
        assert_eq!(adler32(b""), 1);
    }

    #[test]
    fn the_header_is_a_png_and_the_chunks_are_where_they_belong() {
        let img = encode(&[0u8; 4 * 3 * 3], 4, 3);
        assert_eq!(&img[..8], &[0x89, b'P', b'N', b'G', 0x0d, 0x0a, 0x1a, 0x0a]);
        assert_eq!(&img[12..16], b"IHDR");
        assert_eq!(u32::from_be_bytes(img[8..12].try_into().unwrap()), 13, "IHDR is 13 bytes");
        assert_eq!(u32::from_be_bytes(img[16..20].try_into().unwrap()), 4, "width");
        assert_eq!(u32::from_be_bytes(img[20..24].try_into().unwrap()), 3, "height");
        assert_eq!(&img[img.len() - 8..img.len() - 4], b"IEND");
    }

    /// Walk the chunk list the way a decoder does and check every CRC. This is the test that
    /// catches a CRC computed over the body without the chunk type — the classic way to write a
    /// PNG no decoder accepts.
    #[test]
    fn every_chunk_crc_verifies() {
        let img = encode(&[7u8; 320 * 240 * 3], 320, 240);
        let mut p = 8;
        let mut kinds = Vec::new();
        while p < img.len() {
            let len = u32::from_be_bytes(img[p..p + 4].try_into().unwrap()) as usize;
            let kind = &img[p + 4..p + 8];
            let want = u32::from_be_bytes(img[p + 8 + len..p + 12 + len].try_into().unwrap());
            assert_eq!(crc32(&img[p + 4..p + 8 + len]), want, "bad CRC on {kind:?}");
            kinds.push(String::from_utf8_lossy(kind).into_owned());
            p += 12 + len;
        }
        assert_eq!(p, img.len(), "trailing bytes after IEND");
        assert_eq!(kinds, ["IHDR", "IDAT", "IEND"]);
    }

    /// A stored deflate block carries `len` and `!len`, and a 320x240 image is 231 040 raw bytes —
    /// over 0xffff, so it takes four blocks and only the last may be flagged final. This is the
    /// arithmetic a one-block encoder gets away with on a small fixture and fails on a real frame.
    #[test]
    fn the_stored_stream_is_framed_correctly_across_block_boundaries() {
        let raw_len = 240 * (1 + 320 * 3);
        assert!(raw_len > 0xffff * 3, "the fixture must span more than three blocks");
        let img = encode(&[0u8; 320 * 240 * 3], 320, 240);
        // IDAT body: skip the 8-byte signature, the 25-byte IHDR chunk, and IDAT's own 8-byte head.
        let idat_len = u32::from_be_bytes(img[33..37].try_into().unwrap()) as usize;
        let z = &img[41..41 + idat_len];
        assert_eq!(&z[..2], &[0x78, 0x01]);
        assert_eq!(
            u16::from_be_bytes([z[0], z[1]]) % 31,
            0,
            "the zlib header's check bits must make it a multiple of 31"
        );
        let mut p = 2;
        let (mut total, mut blocks) = (0usize, 0usize);
        loop {
            let final_block = z[p] & 1 != 0;
            assert_eq!(z[p] & 6, 0, "block type must be 00, stored");
            let n = u16::from_le_bytes([z[p + 1], z[p + 2]]);
            let m = u16::from_le_bytes([z[p + 3], z[p + 4]]);
            assert_eq!(n, !m, "the one's-complement length must agree");
            total += n as usize;
            blocks += 1;
            p += 5 + n as usize;
            if final_block {
                break;
            }
        }
        assert_eq!(total, raw_len, "the blocks must cover every raw byte exactly once");
        assert_eq!(blocks, raw_len.div_ceil(0xffff));
        assert_eq!(p + 4, z.len(), "the Adler sum is the last four bytes and nothing follows it");
        assert_eq!(u32::from_be_bytes(z[p..p + 4].try_into().unwrap()), adler32(&vec![0u8; raw_len]));
    }

    /// A frame that is exactly one raw block short of a boundary. The `while` loop above is written
    /// so the last block is flagged by `done + n == raw.len()`; the obvious alternative,
    /// `(i + 1) * 0xffff >= raw.len()`, flags one block too early when the length divides evenly.
    #[test]
    fn a_raw_length_that_divides_evenly_still_ends_with_exactly_one_final_block() {
        // 1 + w*3 = 0xffff means each scanline is exactly one block.
        let w = (0xffff - 1) / 3;
        let img = encode(&vec![0u8; w * 2 * 3], w, 2);
        let idat_len = u32::from_be_bytes(img[33..37].try_into().unwrap()) as usize;
        let z = &img[41..41 + idat_len];
        let (mut p, mut finals) = (2usize, 0usize);
        loop {
            if z[p] & 1 != 0 {
                finals += 1;
            }
            let n = u16::from_le_bytes([z[p + 1], z[p + 2]]) as usize;
            p += 5 + n;
            if p + 4 >= z.len() {
                break;
            }
        }
        assert_eq!(finals, 1, "exactly one block may carry BFINAL");
        assert_eq!(p + 4, z.len());
    }
}
