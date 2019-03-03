/** @file
 * Copyright (c) 2019, Arm Limited or its affiliates. All rights reserved.
 * SPDX-License-Identifier : Apache-2.0
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *  http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
**/

#include "val_crypto.h"

typedef struct {
    char                    test_desc[75];
    psa_key_handle_t        key_handle;
    psa_key_type_t          key_type;
    uint8_t                 key_data[16];
    uint32_t                key_length;
    psa_key_usage_t         usage;
    psa_algorithm_t         key_alg;
    uint8_t                 salt[16];
    size_t                  salt_length;
    uint8_t                 input[32];
    size_t                  input_length;
    size_t                  output_size;
    size_t                  expected_output_length;
    size_t                  expected_bit_length;
    psa_status_t            expected_status;
} test_data;

static const uint8_t rsa_384_keypair[1];
static const uint8_t rsa_384_keydata[1];
static const uint8_t rsa_256_keypair[1];
static const uint8_t rsa_256_keydata[1];

static const uint8_t ec_keydata[] = {
 0x30, 0x49, 0x30, 0x13, 0x06, 0x07, 0x2A, 0x86, 0x48, 0xCE, 0x3D, 0x02, 0x01, 0x06,
 0x08, 0x2A, 0x86, 0x48, 0xCE, 0x3D, 0x03, 0x01, 0x01, 0x03, 0x32, 0x00, 0x04, 0xBC,
 0x79, 0x7D, 0xB3, 0xAE, 0x7F, 0x08, 0xEC, 0x3D, 0x49, 0x6B, 0x4F, 0xB4, 0x11, 0xB3,
 0xF6, 0x20, 0xA5, 0x58, 0xA5, 0x01, 0xE0, 0x22, 0x2D, 0x08, 0xCF, 0xE0, 0xDC, 0x8A,
 0xEC, 0x8B, 0x1A, 0x7B, 0xF2, 0x4B, 0xE9, 0x29, 0x51, 0xCC, 0x5B, 0xA1, 0xBE, 0xBB,
 0x24, 0x74, 0x90, 0x9A, 0xE0};

static const uint8_t ec_keypair[] = {
 0x30, 0x5F, 0x02, 0x01, 0x01, 0x04, 0x18, 0x33, 0x8E, 0x86, 0xA8, 0x81, 0xE2, 0x38,
 0xF5, 0x49, 0xBD, 0x6F, 0x05, 0x53, 0x49, 0x4B, 0x73, 0xE3, 0xD6, 0x11, 0x30, 0xFD,
 0xC6, 0xC9, 0x6D, 0xA0, 0x0A, 0x06, 0x08, 0x2A, 0x86, 0x48, 0xCE, 0x3D, 0x03, 0x01,
 0x01, 0xA1, 0x34, 0x03, 0x32, 0x00, 0x04, 0x51, 0x75, 0xBC, 0xDF, 0x30, 0xA3, 0x70,
 0xF3, 0x9D, 0x53, 0x93, 0xE6, 0x12, 0x72, 0x88, 0xD8, 0x01, 0x67, 0xB5, 0xF4, 0xB4,
 0xB7, 0x76, 0xC6, 0x74, 0xF7, 0xC6, 0xF3, 0x54, 0xB7, 0xD2, 0x24, 0x06, 0x2C, 0x1F,
 0x68, 0x54, 0xB5, 0xA7, 0xAF, 0x0F, 0xE5, 0x78, 0xEA, 0xF2, 0x58, 0xF0, 0x27};

static const uint8_t rsa_128_keydata[] = {
 0x30, 0x81, 0x9f, 0x30, 0x0d, 0x06, 0x09, 0x2a, 0x86, 0x48, 0x86, 0xf7, 0x0d,
 0x01, 0x01, 0x01, 0x05, 0x00, 0x03, 0x81, 0x8d, 0x00, 0x30, 0x81, 0x89, 0x02,
 0x81, 0x81, 0x00, 0xaf, 0x05, 0x7d, 0x39, 0x6e, 0xe8, 0x4f, 0xb7, 0x5f, 0xdb,
 0xb5, 0xc2, 0xb1, 0x3c, 0x7f, 0xe5, 0xa6, 0x54, 0xaa, 0x8a, 0xa2, 0x47, 0x0b,
 0x54, 0x1e, 0xe1, 0xfe, 0xb0, 0xb1, 0x2d, 0x25, 0xc7, 0x97, 0x11, 0x53, 0x12,
 0x49, 0xe1, 0x12, 0x96, 0x28, 0x04, 0x2d, 0xbb, 0xb6, 0xc1, 0x20, 0xd1, 0x44,
 0x35, 0x24, 0xef, 0x4c, 0x0e, 0x6e, 0x1d, 0x89, 0x56, 0xee, 0xb2, 0x07, 0x7a,
 0xf1, 0x23, 0x49, 0xdd, 0xee, 0xe5, 0x44, 0x83, 0xbc, 0x06, 0xc2, 0xc6, 0x19,
 0x48, 0xcd, 0x02, 0xb2, 0x02, 0xe7, 0x96, 0xae, 0xbd, 0x94, 0xd3, 0xa7, 0xcb,
 0xf8, 0x59, 0xc2, 0xc1, 0x81, 0x9c, 0x32, 0x4c, 0xb8, 0x2b, 0x9c, 0xd3, 0x4e,
 0xde, 0x26, 0x3a, 0x2a, 0xbf, 0xfe, 0x47, 0x33, 0xf0, 0x77, 0x86, 0x9e, 0x86,
 0x60, 0xf7, 0xd6, 0x83, 0x4d, 0xa5, 0x3d, 0x69, 0x0e, 0xf7, 0x98, 0x5f, 0x6b,
 0xc3, 0x02, 0x03, 0x01, 0x00, 0x01};

static const uint8_t rsa_128_keypair[] = {
0x30, 0x82, 0x02, 0x5e, 0x02, 0x01, 0x00, 0x02, 0x81, 0x81, 0x00, 0xaf, 0x05,
0x7d, 0x39, 0x6e, 0xe8, 0x4f, 0xb7, 0x5f, 0xdb, 0xb5, 0xc2, 0xb1, 0x3c, 0x7f,
0xe5, 0xa6, 0x54, 0xaa, 0x8a, 0xa2, 0x47, 0x0b, 0x54, 0x1e, 0xe1, 0xfe, 0xb0,
0xb1, 0x2d, 0x25, 0xc7, 0x97, 0x11, 0x53, 0x12, 0x49, 0xe1, 0x12, 0x96, 0x28,
0x04, 0x2d, 0xbb, 0xb6, 0xc1, 0x20, 0xd1, 0x44, 0x35, 0x24, 0xef, 0x4c, 0x0e,
0x6e, 0x1d, 0x89, 0x56, 0xee, 0xb2, 0x07, 0x7a, 0xf1, 0x23, 0x49, 0xdd, 0xee,
0xe5, 0x44, 0x83, 0xbc, 0x06, 0xc2, 0xc6, 0x19, 0x48, 0xcd, 0x02, 0xb2, 0x02,
0xe7, 0x96, 0xae, 0xbd, 0x94, 0xd3, 0xa7, 0xcb, 0xf8, 0x59, 0xc2, 0xc1, 0x81,
0x9c, 0x32, 0x4c, 0xb8, 0x2b, 0x9c, 0xd3, 0x4e, 0xde, 0x26, 0x3a, 0x2a, 0xbf,
0xfe, 0x47, 0x33, 0xf0, 0x77, 0x86, 0x9e, 0x86, 0x60, 0xf7, 0xd6, 0x83, 0x4d,
0xa5, 0x3d, 0x69, 0x0e, 0xf7, 0x98, 0x5f, 0x6b, 0xc3, 0x02, 0x03, 0x01, 0x00,
0x01, 0x02, 0x81, 0x81, 0x00, 0x87, 0x4b, 0xf0, 0xff, 0xc2, 0xf2, 0xa7, 0x1d,
0x14, 0x67, 0x1d, 0xdd, 0x01, 0x71, 0xc9, 0x54, 0xd7, 0xfd, 0xbf, 0x50, 0x28,
0x1e, 0x4f, 0x6d, 0x99, 0xea, 0x0e, 0x1e, 0xbc, 0xf8, 0x2f, 0xaa, 0x58, 0xe7,
0xb5, 0x95, 0xff, 0xb2, 0x93, 0xd1, 0xab, 0xe1, 0x7f, 0x11, 0x0b, 0x37, 0xc4,
0x8c, 0xc0, 0xf3, 0x6c, 0x37, 0xe8, 0x4d, 0x87, 0x66, 0x21, 0xd3, 0x27, 0xf6,
0x4b, 0xbe, 0x08, 0x45, 0x7d, 0x3e, 0xc4, 0x09, 0x8b, 0xa2, 0xfa, 0x0a, 0x31,
0x9f, 0xba, 0x41, 0x1c, 0x28, 0x41, 0xed, 0x7b, 0xe8, 0x31, 0x96, 0xa8, 0xcd,
0xf9, 0xda, 0xa5, 0xd0, 0x06, 0x94, 0xbc, 0x33, 0x5f, 0xc4, 0xc3, 0x22, 0x17,
0xfe, 0x04, 0x88, 0xbc, 0xe9, 0xcb, 0x72, 0x02, 0xe5, 0x94, 0x68, 0xb1, 0xea,
0xd1, 0x19, 0x00, 0x04, 0x77, 0xdb, 0x2c, 0xa7, 0x97, 0xfa, 0xc1, 0x9e, 0xda,
0x3f, 0x58, 0xc1, 0x02, 0x41, 0x00, 0xe2, 0xab, 0x76, 0x08, 0x41, 0xbb, 0x9d,
0x30, 0xa8, 0x1d, 0x22, 0x2d, 0xe1, 0xeb, 0x73, 0x81, 0xd8, 0x22, 0x14, 0x40,
0x7f, 0x1b, 0x97, 0x5c, 0xbb, 0xfe, 0x4e, 0x1a, 0x94, 0x67, 0xfd, 0x98, 0xad,
0xbd, 0x78, 0xf6, 0x07, 0x83, 0x6c, 0xa5, 0xbe, 0x19, 0x28, 0xb9, 0xd1, 0x60,
0xd9, 0x7f, 0xd4, 0x5c, 0x12, 0xd6, 0xb5, 0x2e, 0x2c, 0x98, 0x71, 0xa1, 0x74,
0xc6, 0x6b, 0x48, 0x81, 0x13, 0x02, 0x41, 0x00, 0xc5, 0xab, 0x27, 0x60, 0x21,
0x59, 0xae, 0x7d, 0x6f, 0x20, 0xc3, 0xc2, 0xee, 0x85, 0x1e, 0x46, 0xdc, 0x11,
0x2e, 0x68, 0x9e, 0x28, 0xd5, 0xfc, 0xbb, 0xf9, 0x90, 0xa9, 0x9e, 0xf8, 0xa9,
0x0b, 0x8b, 0xb4, 0x4f, 0xd3, 0x64, 0x67, 0xe7, 0xfc, 0x17, 0x89, 0xce, 0xb6,
0x63, 0xab, 0xda, 0x33, 0x86, 0x52, 0xc3, 0xc7, 0x3f, 0x11, 0x17, 0x74, 0x90,
0x2e, 0x84, 0x05, 0x65, 0x92, 0x70, 0x91, 0x02, 0x41, 0x00, 0xb6, 0xcd, 0xbd,
0x35, 0x4f, 0x7d, 0xf5, 0x79, 0xa6, 0x3b, 0x48, 0xb3, 0x64, 0x3e, 0x35, 0x3b,
0x84, 0x89, 0x87, 0x77, 0xb4, 0x8b, 0x15, 0xf9, 0x4e, 0x0b, 0xfc, 0x05, 0x67,
0xa6, 0xae, 0x59, 0x11, 0xd5, 0x7a, 0xd6, 0x40, 0x9c, 0xf7, 0x64, 0x7b, 0xf9,
0x62, 0x64, 0xe9, 0xbd, 0x87, 0xeb, 0x95, 0xe2, 0x63, 0xb7, 0x11, 0x0b, 0x9a,
0x1f, 0x9f, 0x94, 0xac, 0xce, 0xd0, 0xfa, 0xfa, 0x4d, 0x02, 0x40, 0x71, 0x19,
0x5e, 0xec, 0x37, 0xe8, 0xd2, 0x57, 0xde, 0xcf, 0xc6, 0x72, 0xb0, 0x7a, 0xe6,
0x39, 0xf1, 0x0c, 0xbb, 0x9b, 0x0c, 0x73, 0x9d, 0x0c, 0x80, 0x99, 0x68, 0xd6,
0x44, 0xa9, 0x4e, 0x3f, 0xd6, 0xed, 0x92, 0x87, 0x07, 0x7a, 0x14, 0x58, 0x3f,
0x37, 0x90, 0x58, 0xf7, 0x6a, 0x8a, 0xec, 0xd4, 0x3c, 0x62, 0xdc, 0x8c, 0x0f,
0x41, 0x76, 0x66, 0x50, 0xd7, 0x25, 0x27, 0x5a, 0xc4, 0xa1, 0x02, 0x41, 0x00,
0xbb, 0x32, 0xd1, 0x33, 0xed, 0xc2, 0xe0, 0x48, 0xd4, 0x63, 0x38, 0x8b, 0x7b,
0xe9, 0xcb, 0x4b, 0xe2, 0x9f, 0x4b, 0x62, 0x50, 0xbe, 0x60, 0x3e, 0x70, 0xe3,
0x64, 0x75, 0x01, 0xc9, 0x7d, 0xdd, 0xe2, 0x0a, 0x4e, 0x71, 0xbe, 0x95, 0xfd,
0x5e, 0x71, 0x78, 0x4e, 0x25, 0xac, 0xa4, 0xba, 0xf2, 0x5b, 0xe5, 0x73, 0x8a,
0xae, 0x59, 0xbb, 0xfe, 0x1c, 0x99, 0x77, 0x81, 0x44, 0x7a, 0x2b, 0x24};

static test_data check1[] = {
#ifdef ARCH_TEST_RSA_1024
#ifdef ARCH_TEST_RSA_PKCS1V15_CRYPT
{"Test psa_asymmetric_encrypt - RSA PKCS1V15\n", 1, PSA_KEY_TYPE_RSA_PUBLIC_KEY,
{0}, 162, PSA_KEY_USAGE_ENCRYPT, PSA_ALG_RSA_PKCS1V15_CRYPT,
{0}, 0,
{0xba, 0x78, 0x16, 0xbf, 0x8f, 0x01, 0xcf, 0xea, 0x41, 0x41, 0x40, 0xde, 0x5d,
 0xae, 0x22, 0x23, 0xb0, 0x03, 0x61, 0xa3, 0x96, 0x17, 0x7a, 0x9c, 0xb4, 0x10,
 0xff, 0x61, 0xf2, 0x00, 0x15, 0xad}, 22, 128,
 128, 1024, PSA_SUCCESS
},
#endif

#ifdef ARCH_TEST_SHA256
#ifdef ARCH_TEST_RSA_OAEP
{"Test psa_asymmetric_encrypt - RSA OAEP SHA256\n", 2, PSA_KEY_TYPE_RSA_PUBLIC_KEY,
{0}, 162, PSA_KEY_USAGE_ENCRYPT, PSA_ALG_RSA_OAEP(PSA_ALG_SHA_256),
{0}, 0,
{0xba, 0x78, 0x16, 0xbf, 0x8f, 0x01, 0xcf, 0xea, 0x41, 0x41, 0x40, 0xde, 0x5d,
 0xae, 0x22, 0x23, 0xb0, 0x03, 0x61, 0xa3, 0x96, 0x17, 0x7a, 0x9c, 0xb4, 0x10,
 0xff, 0x61, 0xf2, 0x00, 0x15, 0xad}, 22, 128,
 128, 1024, PSA_SUCCESS
},

{"Test psa_asymmetric_encrypt - RSA OAEP SHA256 with label\n", 3, PSA_KEY_TYPE_RSA_PUBLIC_KEY,
{0}, 162, PSA_KEY_USAGE_ENCRYPT, PSA_ALG_RSA_OAEP(PSA_ALG_SHA_256),
{0x74, 0x68, 0x69, 0x73, 0x00, 0x69, 0x73, 0x00, 0x61, 0x00, 0x6c, 0x61, 0x62,
 0x65, 0x6c, 0x00}, 16,
{0xba, 0x78, 0x16, 0xbf, 0x8f, 0x01, 0xcf, 0xea, 0x41, 0x41, 0x40, 0xde, 0x5d,
 0xae, 0x22, 0x23, 0xb0, 0x03, 0x61, 0xa3, 0x96, 0x17, 0x7a, 0x9c, 0xb4, 0x10,
 0xff, 0x61, 0xf2, 0x00, 0x15, 0xad}, 22, 128,
 128, 1024, PSA_SUCCESS
},
#endif
#endif

#ifdef ARCH_TEST_RSA_PKCS1V15_CRYPT
{"Test psa_asymmetric_encrypt - RSA KEYPAIR PKCS1V15\n", 4, PSA_KEY_TYPE_RSA_KEYPAIR,
{0}, 610, PSA_KEY_USAGE_ENCRYPT | PSA_KEY_USAGE_DECRYPT, PSA_ALG_RSA_PKCS1V15_CRYPT,
{0}, 0,
{0xba, 0x78, 0x16, 0xbf, 0x8f, 0x01, 0xcf, 0xea, 0x41, 0x41, 0x40, 0xde, 0x5d,
 0xae, 0x22, 0x23, 0xb0, 0x03, 0x61, 0xa3, 0x96, 0x17, 0x7a, 0x9c, 0xb4, 0x10,
 0xff, 0x61, 0xf2, 0x00, 0x15, 0xad}, 22, 128,
 128, 1024, PSA_SUCCESS
},

{"Test psa_asymmetric_encrypt - Small output buffer\n", 5, PSA_KEY_TYPE_RSA_PUBLIC_KEY,
{0}, 162, PSA_KEY_USAGE_ENCRYPT | PSA_KEY_USAGE_DECRYPT, PSA_ALG_RSA_PKCS1V15_CRYPT,
{0}, 0,
{0xba, 0x78, 0x16, 0xbf, 0x8f, 0x01, 0xcf, 0xea, 0x41, 0x41, 0x40, 0xde, 0x5d,
 0xae, 0x22, 0x23, 0xb0, 0x03, 0x61, 0xa3, 0x96, 0x17, 0x7a, 0x9c, 0xb4, 0x10,
 0xff, 0x61, 0xf2, 0x00, 0x15, 0xad}, 22, 110,
 128, 1024, PSA_ERROR_INVALID_ARGUMENT
},
#endif

#ifdef ARCH_TEST_SHA256
{"Test psa_asymmetric_encrypt - Invalid algorithm\n", 6, PSA_KEY_TYPE_RSA_PUBLIC_KEY,
{0}, 162, PSA_KEY_USAGE_ENCRYPT, PSA_ALG_SHA_256,
{0}, 0,
{0xba, 0x78, 0x16, 0xbf, 0x8f, 0x01, 0xcf, 0xea, 0x41, 0x41, 0x40, 0xde, 0x5d,
 0xae, 0x22, 0x23, 0xb0, 0x03, 0x61, 0xa3, 0x96, 0x17, 0x7a, 0x9c, 0xb4, 0x10,
 0xff, 0x61, 0xf2, 0x00, 0x15, 0xad}, 22, 128,
 128, 1024, PSA_ERROR_INVALID_ARGUMENT
},
#endif
#endif

#ifdef ARCH_TEST_RSA_PKCS1V15_CRYPT
#ifdef ARCH_TEST_AES_128
{"Test psa_asymmetric_encrypt - Invalid key type\n", 7, PSA_KEY_TYPE_AES,
{0x30, 0x82, 0x02, 0x5e, 0x02, 0x01, 0x00, 0x02, 0x81, 0x81, 0x00, 0xaf, 0x05,
 0x7d, 0x39, 0x6e}, 16,
 PSA_KEY_USAGE_ENCRYPT, PSA_ALG_RSA_PKCS1V15_CRYPT,
{0}, 0,
{0xba, 0x78, 0x16, 0xbf, 0x8f, 0x01, 0xcf, 0xea, 0x41, 0x41, 0x40, 0xde, 0x5d,
 0xae, 0x22, 0x23, 0xb0, 0x03, 0x61, 0xa3, 0x96, 0x17, 0x7a, 0x9c, 0xb4, 0x10,
 0xff, 0x61, 0xf2, 0x00, 0x15, 0xad}, 22, 128,
 0, BYTES_TO_BITS(AES_16B_KEY_SIZE), PSA_ERROR_INVALID_ARGUMENT
},
#endif

#ifdef ARCH_TEST_RSA_1024
{"Test psa_asymmetric_encrypt - Invalid usage\n", 8, PSA_KEY_TYPE_RSA_PUBLIC_KEY,
{0}, 162, PSA_KEY_USAGE_DECRYPT, PSA_ALG_RSA_PKCS1V15_CRYPT,
{0}, 0,
{0xba, 0x78, 0x16, 0xbf, 0x8f, 0x01, 0xcf, 0xea, 0x41, 0x41, 0x40, 0xde, 0x5d,
 0xae, 0x22, 0x23, 0xb0, 0x03, 0x61, 0xa3, 0x96, 0x17, 0x7a, 0x9c, 0xb4, 0x10,
 0xff, 0x61, 0xf2, 0x00, 0x15, 0xad}, 22, 128,
 128, 1024, PSA_ERROR_NOT_PERMITTED
},
#endif
#endif

#ifdef FUTURE_SUPPORT
{"Test psa_asymmetric_encrypt - ECC public key\n", 9,
 PSA_KEY_TYPE_ECC_PUBLIC_KEY_BASE | PSA_ECC_CURVE_SECP192R1,
{0}, 75, PSA_KEY_USAGE_ENCRYPT, PSA_ALG_ECDSA_ANY,
{0}, 0,
{0xba, 0x78, 0x16, 0xbf, 0x8f, 0x01, 0xcf, 0xea, 0x41, 0x41, 0x40, 0xde, 0x5d,
 0xae, 0x22, 0x23, 0xb0, 0x03, 0x61, 0xa3, 0x96, 0x17, 0x7a, 0x9c, 0xb4, 0x10,
 0xff, 0x61, 0xf2, 0x00, 0x15, 0xad}, 22, 128,
 0, 192, PSA_SUCCESS
}

{"Test psa_asymmetric_encrypt - ECC keypair\n", 10,
 PSA_KEY_TYPE_ECC_KEYPAIR(PSA_ECC_CURVE_SECP256R1),
{0}, 97, PSA_KEY_USAGE_ENCRYPT, PSA_ALG_DETERMINISTIC_ECDSA(PSA_ALG_SHA_256),
{0}, 0,
{0xba, 0x78, 0x16, 0xbf, 0x8f, 0x01, 0xcf, 0xea, 0x41, 0x41, 0x40, 0xde, 0x5d,
 0xae, 0x22, 0x23, 0xb0, 0x03, 0x61, 0xa3, 0x96, 0x17, 0x7a, 0x9c, 0xb4, 0x10,
 0xff, 0x61, 0xf2, 0x00, 0x15, 0xad}, 22, 128,
 0, 192, PSA_SUCCESS
}
#endif
};

static test_data check2[] = {
#ifdef ARCH_TEST_RSA_PKCS1V15_CRYPT
#ifdef ARCH_TEST_RSA_1024
{"Test psa_asymmetric_encrypt - Negative case\n", 11, PSA_KEY_TYPE_RSA_PUBLIC_KEY,
{0}, 162, PSA_KEY_USAGE_ENCRYPT, PSA_ALG_RSA_PKCS1V15_CRYPT,
{0}, 0,
{0xba, 0x78, 0x16, 0xbf, 0x8f, 0x01, 0xcf, 0xea, 0x41, 0x41, 0x40, 0xde, 0x5d,
 0xae, 0x22, 0x23, 0xb0, 0x03, 0x61, 0xa3, 0x96, 0x17, 0x7a, 0x9c, 0xb4, 0x10,
 0xff, 0x61, 0xf2, 0x00, 0x15, 0xad}, 22, 128,
 128, 1024, PSA_SUCCESS
},
#endif
#endif
};
