/*
 * Copyright (c) 2017, [Ribose Inc](https://www.ribose.com).
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDERS OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <botan/ffi.h>
#include "ec.h"
#include "types.h"
#include "utils.h"

/**
 * EC Curves definition used by implementation
 *
 * \see RFC4880 bis01 - 9.2. ECC Curve OID
 *
 * Order of the elements in this array corresponds to
 * values in pgp_curve_t enum.
 */
static const ec_curve_desc_t ec_curves[] = {
  {PGP_CURVE_UNKNOWN, 0, {0}, 0, NULL, NULL},

  {PGP_CURVE_NIST_P_256,
   256,
   {0x2A, 0x86, 0x48, 0xCE, 0x3D, 0x03, 0x01, 0x07},
   8,
   "secp256r1",
   "NIST P-256"},
  {PGP_CURVE_NIST_P_384, 384, {0x2B, 0x81, 0x04, 0x00, 0x22}, 5, "secp384r1", "NIST P-384"},
  {PGP_CURVE_NIST_P_521, 521, {0x2B, 0x81, 0x04, 0x00, 0x23}, 5, "secp521r1", "NIST P-521"},
  {PGP_CURVE_ED25519,
   255,
   {0x2b, 0x06, 0x01, 0x04, 0x01, 0xda, 0x47, 0x0f, 0x01},
   9,
   "Ed25519",
   "Ed25519"},
  {PGP_CURVE_SM2_P_256,
   256,
   {0x2A, 0x81, 0x1C, 0xCF, 0x55, 0x01, 0x82, 0x2D},
   8,
   "sm2p256v1",
   "SM2 P-256"},
};

static pgp_map_t ec_algo_to_botan[] = {
  {PGP_PKA_ECDH, "ECDH"},
  {PGP_PKA_ECDSA, "ECDSA"},
  {PGP_PKA_SM2, "SM2_Sig"},
};

pgp_curve_t
find_curve_by_OID(const uint8_t *oid, size_t oid_len)
{
    for (size_t i = 0; i < PGP_CURVE_MAX; i++) {
        if ((oid_len == ec_curves[i].OIDhex_len) &&
            (!memcmp(oid, ec_curves[i].OIDhex, oid_len))) {
            return static_cast<pgp_curve_t>(i);
        }
    }

    return PGP_CURVE_MAX;
}

pgp_curve_t
find_curve_by_name(const char *name)
{
    for (size_t i = 1; i < PGP_CURVE_MAX; i++) {
        if (!strcmp(ec_curves[i].pgp_name, name)) {
            return ec_curves[i].rnp_curve_id;
        }
    }

    return PGP_CURVE_MAX;
}

const ec_curve_desc_t *
get_curve_desc(const pgp_curve_t curve_id)
{
    return (curve_id < PGP_CURVE_MAX && curve_id > 0) ? &ec_curves[curve_id] : NULL;
}

rnp_result_t
ec_generate(rng_t *                rng,
            pgp_ec_key_t *         key,
            const pgp_pubkey_alg_t alg_id,
            const pgp_curve_t      curve)
{
    /**
     * Keeps "0x04 || x || y"
     * \see 13.2.  ECDSA, ECDH, SM2 Conversion Primitives
     *
     * P-521 is biggest supported curve
     */
    botan_privkey_t        pr_key = NULL;
    botan_pubkey_t         pu_key = NULL;
    bignum_t *             px = NULL;
    bignum_t *             py = NULL;
    bignum_t *             x = NULL;
    rnp_result_t           ret = RNP_ERROR_KEY_GENERATION;
    size_t                 filed_byte_size = 0;
    const ec_curve_desc_t *ec_desc = get_curve_desc(curve);
    if (!ec_desc) {
        ret = RNP_ERROR_BAD_PARAMETERS;
        goto end;
    }
    filed_byte_size = BITS_TO_BYTES(ec_desc->bitlen);

    // at this point it must succeed
    if (botan_privkey_create(&pr_key,
                             pgp_str_from_map(alg_id, ec_algo_to_botan),
                             ec_desc->botan_name,
                             rng_handle(rng))) {
        goto end;
    }

    if (botan_privkey_export_pubkey(&pu_key, pr_key)) {
        goto end;
    }

    // Crash if seckey is null. It's clean and easy to debug design
    px = bn_new();
    py = bn_new();
    x = bn_new();

    if (!px || !py || !x) {
        RNP_LOG("Allocation failed");
        ret = RNP_ERROR_OUT_OF_MEMORY;
        goto end;
    }

    if (botan_pubkey_get_field(BN_HANDLE_PTR(px), pu_key, "public_x")) {
        goto end;
    }

    if (botan_pubkey_get_field(BN_HANDLE_PTR(py), pu_key, "public_y")) {
        goto end;
    }

    if (botan_privkey_get_field(BN_HANDLE_PTR(x), pr_key, "x")) {
        goto end;
    }

    size_t x_bytes;
    size_t y_bytes;
    (void) bn_num_bytes(px, &x_bytes); // Can't fail
    (void) bn_num_bytes(py, &y_bytes);

    // Safety check
    if ((x_bytes > filed_byte_size) || (y_bytes > filed_byte_size)) {
        RNP_LOG("Key generation failed");
        ret = RNP_ERROR_BAD_PARAMETERS;
        goto end;
    }

    /*
     * Convert coordinates to MPI stored as
     * "0x04 || x || y"
     *
     *  \see 13.2.  ECDSA and ECDH Conversion Primitives
     *
     * Note: Generated pk/sk may not always have exact number of bytes
     *       which is important when converting to octet-string
     */
    memset(key->p.mpi, 0, sizeof(key->p.mpi));
    key->p.mpi[0] = 0x04;
    bn_bn2bin(px, &key->p.mpi[1 + filed_byte_size - x_bytes]);
    bn_bn2bin(py, &key->p.mpi[1 + filed_byte_size + (filed_byte_size - y_bytes)]);
    key->p.len = 2 * filed_byte_size + 1;
    /* secret key value */
    bn2mpi(x, &key->x);
    ret = RNP_SUCCESS;
end:
    botan_privkey_destroy(pr_key);
    botan_pubkey_destroy(pu_key);
    bn_free(px);
    bn_free(py);
    bn_free(x);
    return ret;
}
