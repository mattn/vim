/* vi:set ts=8 sts=4 sw=4 noet:
 *
 * VIM - Vi IMproved	by Bram Moolenaar
 *
 * Do ":help uganda"  in Vim to read copying and usage conditions.
 * Do ":help credits" in Vim to see a list of people who contributed.
 * See README.txt for an overview of the Vim source code.
 */

/*
 * sixel.c: RGB to DEC sixel encoder.
 *
 * The encoder is intentionally self-contained (no libsixel dependency).
 * The fast path builds a palette on the fly from unique 24-bit colors and
 * fails over to a fixed 6x6x6 + grayscale 256-color palette when the input
 * has more colors than fit in the dynamic palette.
 *
 * Algorithm reference: github.com/mattn/go-sixel (sixel.go).
 */

#include "vim.h"

#if defined(FEAT_SIXEL) || defined(PROTO)

// Palette size cap (sixel allows up to 256 color registers; index 0 is
// reserved as a transparent key, so usable colors are 1..MAX_COLORS).
#define SIXEL_MAX_COLORS    255

// Working buffer for one band (6 pixel rows). Allocated as
// width * (palette_size+1) bytes; bit p (0..5) marks pixel at row offset p.
typedef struct
{
    char_u  *bits;	    // bitmask buffer, width * paletteSize bytes
    int	    *seen;	    // per-color "used in this band" flags
    int	     seen_gen;	    // generation counter to avoid memset
} sixel_band_T;

/*
 * Append a string to a growarray of bytes.  Returns FAIL on OOM.
 */
    static int
ga_concat_bytes(garray_T *gap, const char *s, int len)
{
    if (ga_grow(gap, len) == FAIL)
	return FAIL;
    mch_memmove((char_u *)gap->ga_data + gap->ga_len, s, len);
    gap->ga_len += len;
    return OK;
}

    static int
ga_concat_int(garray_T *gap, int n)
{
    char    buf[16];
    int	    len = vim_snprintf(buf, sizeof(buf), "%d", n);

    return ga_concat_bytes(gap, buf, len);
}

/*
 * Nearest-neighbor RGB resize.  Source and destination are tightly packed
 * R,G,B byte triples.  Returns a malloced buffer of size dw*dh*3.  When
 * sw==dw && sh==dh, returns a plain copy.  Returns NULL on OOM.
 */
    char_u *
sixel_resize_rgb(char_u *src, int sw, int sh, int dw, int dh)
{
    char_u  *dst;
    int	     x, y;

    if (src == NULL || sw <= 0 || sh <= 0 || dw <= 0 || dh <= 0)
	return NULL;

    dst = alloc((size_t)dw * dh * 3);
    if (dst == NULL)
	return NULL;

    if (sw == dw && sh == dh)
    {
	mch_memmove(dst, src, (size_t)dw * dh * 3);
	return dst;
    }

    for (y = 0; y < dh; y++)
    {
	int	    sy = (int)((long long)y * sh / dh);
	char_u	    *srow = src + (size_t)sy * sw * 3;
	char_u	    *drow = dst + (size_t)y * dw * 3;

	for (x = 0; x < dw; x++)
	{
	    int	    sx = (int)((long long)x * sw / dw);
	    char_u  *sp = srow + sx * 3;
	    char_u  *dp = drow + x * 3;

	    dp[0] = sp[0];
	    dp[1] = sp[1];
	    dp[2] = sp[2];
	}
    }
    return dst;
}

/*
 * Build a paletted image from an RGB buffer using on-the-fly hashing.
 * On success: *pal_out receives a malloced array of (npal*3) bytes (R,G,B
 * triples) and *idx_out receives a malloced array of width*height bytes
 * (indices 1..npal; 0 is reserved as transparent and unused here).
 *
 * Returns OK on success, FAIL when colors exceed max_colors (caller may
 * fall back to a fixed palette) or on OOM.
 */
    static int
rgb_to_paletted_fast(
	char_u	*rgb,
	int	 width,
	int	 height,
	int	 max_colors,
	char_u **pal_out,
	int	*npal_out,
	char_u **idx_out)
{
    int		     cap = 1024;
    int		     mask;
    int		     used = 0;
    unsigned int    *keys;	    // 0 == empty; we store key+1 to avoid 0
    char_u	    *vals;
    char_u	    *idx;
    char_u	    *pal;
    int		     i, n;

    while (cap < max_colors * 4)
	cap <<= 1;
    mask = cap - 1;

    keys = ALLOC_CLEAR_MULT(unsigned int, cap);
    vals = ALLOC_CLEAR_MULT(char_u, cap);
    idx = alloc((size_t)width * height);
    pal = alloc((size_t)max_colors * 3);
    if (keys == NULL || vals == NULL || idx == NULL || pal == NULL)
	goto fail;

    n = width * height;
    for (i = 0; i < n; i++)
    {
	char_u		*p = rgb + (size_t)i * 3;
	unsigned int	 key = ((unsigned int)p[0] << 16)
				| ((unsigned int)p[1] << 8)
				| (unsigned int)p[2];
	unsigned int	 h = key * 2654435761u;
	int		 slot = (int)(h & mask);

	for (;;)
	{
	    if (keys[slot] == 0)
	    {
		// new color
		if (used >= max_colors)
		    goto too_many;
		keys[slot] = key + 1;
		vals[slot] = (char_u)(used + 1);    // 1-based palette index
		pal[used * 3]	  = p[0];
		pal[used * 3 + 1] = p[1];
		pal[used * 3 + 2] = p[2];
		used++;
		idx[i] = vals[slot];
		break;
	    }
	    if (keys[slot] == key + 1)
	    {
		idx[i] = vals[slot];
		break;
	    }
	    slot = (slot + 1) & mask;
	}
    }
    vim_free(keys);
    vim_free(vals);
    *pal_out = pal;
    *npal_out = used;
    *idx_out = idx;
    return OK;

too_many:
    vim_free(keys);
    vim_free(vals);
    vim_free(idx);
    vim_free(pal);
    return FAIL;

fail:
    vim_free(keys);
    vim_free(vals);
    vim_free(idx);
    vim_free(pal);
    return FAIL;
}

/*
 * Fallback: quantize RGB to fixed 6x6x6 RGB cube + 24-step grayscale.
 * Always succeeds; produces 240 palette entries.
 */
    static int
rgb_to_paletted_fixed(
	char_u	*rgb,
	int	 width,
	int	 height,
	char_u **pal_out,
	int	*npal_out,
	char_u **idx_out)
{
    char_u  *pal = alloc(240 * 3);
    char_u  *idx = alloc((size_t)width * height);
    int	     r, g, b, gr, n;

    if (pal == NULL || idx == NULL)
    {
	vim_free(pal);
	vim_free(idx);
	return FAIL;
    }

    // 6x6x6 cube: index 0..215
    n = 0;
    for (r = 0; r < 6; r++)
	for (g = 0; g < 6; g++)
	    for (b = 0; b < 6; b++)
	    {
		pal[n * 3]     = (char_u)(r * 51);
		pal[n * 3 + 1] = (char_u)(g * 51);
		pal[n * 3 + 2] = (char_u)(b * 51);
		n++;
	    }
    // grayscale: index 216..239
    for (gr = 0; gr < 24; gr++)
    {
	int v = 8 + gr * 10;	    // 8, 18, ..., 238
	pal[n * 3]     = (char_u)v;
	pal[n * 3 + 1] = (char_u)v;
	pal[n * 3 + 2] = (char_u)v;
	n++;
    }

    // map every pixel to the nearest cube cell
    for (n = 0; n < width * height; n++)
    {
	int ri = rgb[n * 3] / 43;
	int gi = rgb[n * 3 + 1] / 43;
	int bi = rgb[n * 3 + 2] / 43;

	if (ri > 5) ri = 5;
	if (gi > 5) gi = 5;
	if (bi > 5) bi = 5;
	idx[n] = (char_u)(ri * 36 + gi * 6 + bi + 1);	// 1-based
    }

    *pal_out = pal;
    *npal_out = 240;
    *idx_out = idx;
    return OK;
}

/*
 * Emit a run of `cnt` identical sixel data bytes (ch in 0..63) into gap.
 * Uses RLE form `!N{c}` when it shortens the output.
 */
    static int
emit_run(garray_T *gap, char_u ch, int cnt)
{
    char_u  c = (char_u)(63 + ch);

    while (cnt > 255)
    {
	if (ga_concat_bytes(gap, "!255", 4) == FAIL
		|| ga_append(gap, c) == FAIL)
	    return FAIL;
	cnt -= 255;
    }
    if (cnt <= 0)
	return OK;
    if (cnt <= 3)
    {
	while (cnt-- > 0)
	    if (ga_append(gap, c) == FAIL)
		return FAIL;
	return OK;
    }
    if (ga_append(gap, '!') == FAIL
	    || ga_concat_int(gap, cnt) == FAIL
	    || ga_append(gap, c) == FAIL)
	return FAIL;
    return OK;
}

/*
 * Encode an RGB image into a sixel DCS sequence.
 * Returns a malloced char_u* containing the full sequence
 * (\033P...\033\\), or NULL on OOM.
 */
    char_u *
sixel_encode(sixel_image_T *img)
{
    garray_T	ga;
    char_u	*pal = NULL;
    char_u	*idx = NULL;
    int		npal = 0;
    int		width, height;
    int		band, p, x, n;
    char_u	*bits = NULL;
    int		*seen = NULL;
    char_u	*result;

    if (img == NULL || img->data == NULL || img->width <= 0 || img->height <= 0)
	return NULL;
    width = img->width;
    height = img->height;

    if (rgb_to_paletted_fast(img->data, width, height, SIXEL_MAX_COLORS,
		&pal, &npal, &idx) == FAIL)
    {
	if (rgb_to_paletted_fixed(img->data, width, height,
		    &pal, &npal, &idx) == FAIL)
	    return NULL;
    }

    ga_init2(&ga, sizeof(char_u), 4096);

    // DECSIXEL Introducer + Raster Attributes "1;1;W;H
    if (ga_concat_bytes(&ga, "\033P0;0;8q\"1;1;", 13) == FAIL
	    || ga_concat_int(&ga, width) == FAIL
	    || ga_append(&ga, ';') == FAIL
	    || ga_concat_int(&ga, height) == FAIL)
	goto fail;

    // Color register definitions  #N;2;R;G;B  (RGB scaled to 0..100)
    for (n = 0; n < npal; n++)
    {
	int r = pal[n * 3]     * 100 / 255;
	int g = pal[n * 3 + 1] * 100 / 255;
	int b = pal[n * 3 + 2] * 100 / 255;

	if (ga_append(&ga, '#') == FAIL
		|| ga_concat_int(&ga, n + 1) == FAIL
		|| ga_concat_bytes(&ga, ";2;", 3) == FAIL
		|| ga_concat_int(&ga, r) == FAIL
		|| ga_append(&ga, ';') == FAIL
		|| ga_concat_int(&ga, g) == FAIL
		|| ga_append(&ga, ';') == FAIL
		|| ga_concat_int(&ga, b) == FAIL)
	    goto fail;
    }

    // bitmask buffer: width bytes per palette index, +1 for the unused index 0
    bits = ALLOC_CLEAR_MULT(char_u, (size_t)width * (npal + 1));
    seen = ALLOC_CLEAR_MULT(int, npal + 1);
    if (bits == NULL || seen == NULL)
	goto fail;

    for (band = 0; band < (height + 5) / 6; band++)
    {
	int gen = band + 1;
	int last_was_cr = 0;

	if (band > 0 && ga_append(&ga, '-') == FAIL)
	    goto fail;

	// Fill the bitmask for this band.
	for (p = 0; p < 6; p++)
	{
	    int y = band * 6 + p;
	    char_u  rowmask = (char_u)(1 << p);
	    char_u  *row;

	    if (y >= height)
		continue;
	    row = idx + (size_t)y * width;
	    for (x = 0; x < width; x++)
	    {
		char_u  pix = row[x];

		if (pix == 0)
		    continue;
		seen[pix] = gen;
		bits[(size_t)pix * width + x] |= rowmask;
	    }
	}

	for (n = 1; n <= npal; n++)
	{
	    char_u  *row;
	    char_u   ch0;
	    int	     cnt;

	    if (seen[n] != gen)
		continue;
	    if (last_was_cr && ga_append(&ga, '$') == FAIL)
		goto fail;
	    if (ga_append(&ga, '#') == FAIL
		    || ga_concat_int(&ga, n) == FAIL)
		goto fail;

	    row = bits + (size_t)n * width;
	    ch0 = row[0];
	    cnt = 1;
	    row[0] = 0;
	    for (x = 1; x < width; x++)
	    {
		char_u  ch = row[x];

		row[x] = 0;
		if (ch == ch0)
		{
		    cnt++;
		    continue;
		}
		if (emit_run(&ga, ch0, cnt) == FAIL)
		    goto fail;
		ch0 = ch;
		cnt = 1;
	    }
	    if (emit_run(&ga, ch0, cnt) == FAIL)
		goto fail;
	    last_was_cr = 1;
	}
    }

    // String terminator
    if (ga_concat_bytes(&ga, "\033\\", 2) == FAIL
	    || ga_append(&ga, NUL) == FAIL)
	goto fail;

    vim_free(pal);
    vim_free(idx);
    vim_free(bits);
    vim_free(seen);
    result = (char_u *)ga.ga_data;
    return result;

fail:
    vim_free(pal);
    vim_free(idx);
    vim_free(bits);
    vim_free(seen);
    ga_clear(&ga);
    return NULL;
}

#endif // FEAT_SIXEL
