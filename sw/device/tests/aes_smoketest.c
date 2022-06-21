// Copyright lowRISC contributors.
// Licensed under the Apache License, Version 2.0, see LICENSE for details.
// SPDX-License-Identifier: Apache-2.0

#include "sw/device/lib/base/memory.h"
#include "sw/device/lib/base/mmio.h"
#include "sw/device/lib/dif/dif_aes.h"
#include "sw/device/lib/runtime/log.h"
#include "sw/device/lib/testing/aes_testutils.h"
#include "sw/device/lib/testing/test_framework/check.h"
#include "sw/device/lib/testing/test_framework/ottf_main.h"

#include "hw/top_earlgrey/sw/autogen/top_earlgrey.h"

#if !OT_IS_ENGLISH_BREAKFAST
#include "sw/device/lib/testing/entropy_testutils.h"
#endif

// The following plaintext, key and ciphertext are extracted from Appendix C of
// the Advanced Encryption Standard (AES) FIPS Publication 197 available at
// https://www.nist.gov/publications/advanced-encryption-standard-aes

#define TIMEOUT (1000 * 1000)
#define KEY_LENGTH_IN_BYTES 32
#define TEXT_LENGTH_IN_BYTES 16
#define TEXT_LENGTH_IN_WORDS (TEXT_LENGTH_IN_BYTES / 4)

static const uint32_t kPlainText[TEXT_LENGTH_IN_WORDS] = {
    0x33221100,
    0x77665544,
    0xbbaa9988,
    0xffeeddcc,
};

static const uint8_t kKey[KEY_LENGTH_IN_BYTES] = {
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a,
    0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15,
    0x16, 0x17, 0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f,
};

static const uint32_t kCipherTextGold[TEXT_LENGTH_IN_WORDS] = {
    0xcab7a28e,
    0xbf456751,
    0x9049fcea,
    0x8960494b,
};

// The mask share, used to mask kKey. Note that the masking should not be done
// manually. Software is expected to get the key in two shares right from the
// beginning.
static const uint8_t kKeyShare1[KEY_LENGTH_IN_BYTES] = {
    0x0f, 0x1f, 0x2f, 0x3F, 0x4f, 0x5f, 0x6f, 0x7f, 0x8f, 0x9f, 0xaf,
    0xbf, 0xcf, 0xdf, 0xef, 0xff, 0x0a, 0x1a, 0x2a, 0x3a, 0x4a, 0x5a,
    0x6a, 0x7a, 0x8a, 0x9a, 0xaa, 0xba, 0xca, 0xda, 0xea, 0xfa,
};

const test_config_t kTestConfig;

bool test_main(void) {
  dif_aes_t aes;

  LOG_INFO("Running AES test");

#if !OT_IS_ENGLISH_BREAKFAST
  // First of all, we need to get the entropy complex up and running.
  entropy_testutils_boot_mode_init();
#endif

  // Initialise AES.
  CHECK_DIF_OK(
      dif_aes_init(mmio_region_from_addr(TOP_EARLGREY_AES_BASE_ADDR), &aes));
  CHECK_DIF_OK(dif_aes_reset(&aes));

  // Mask the key. Note that this should not be done manually. Software is
  // expected to get the key in two shares right from the beginning.
  uint8_t key_share0[KEY_LENGTH_IN_BYTES];
  for (int i = 0; i < KEY_LENGTH_IN_BYTES; ++i) {
    key_share0[i] = kKey[i] ^ kKeyShare1[i];
  }

  // "Convert" key share byte arrays to `dif_aes_key_share_t`.
  dif_aes_key_share_t key;
  memcpy(&key.share0[0], &key_share0[0], KEY_LENGTH_IN_BYTES);
  memcpy(&key.share1[0], &kKeyShare1[0], KEY_LENGTH_IN_BYTES);

  // Setup ECB encryption transaction.
  dif_aes_transaction_t transaction = {
      .operation = kDifAesOperationEncrypt,
      .mode = kDifAesModeEcb,
      .key_len = kDifAesKey256,
      .manual_operation = kDifAesManualOperationAuto,
  };
  CHECK_DIF_OK(dif_aes_start(&aes, &transaction, &key, NULL));

  // "Convert" plain data byte arrays to `dif_aes_data_t`.
  dif_aes_data_t in_data_plain;
  memcpy(&in_data_plain.data[0], &kPlainText[0], TEXT_LENGTH_IN_BYTES);

  // Load the plain text to trigger the encryption operation.
  AES_TESTUTILS_WAIT_FOR_STATUS(&aes, kDifAesStatusInputReady, true, TIMEOUT);
  CHECK_DIF_OK(dif_aes_load_data(&aes, in_data_plain));

  // Read out the produced cipher text.
  dif_aes_data_t out_data_cipher;

  AES_TESTUTILS_WAIT_FOR_STATUS(&aes, kDifAesStatusOutputValid, true, TIMEOUT);

  CHECK_DIF_OK(dif_aes_read_output(&aes, &out_data_cipher));

  // Finish the ECB encryption transaction.
  CHECK_DIF_OK(dif_aes_end(&aes));

  CHECK_ARRAYS_EQ(out_data_cipher.data, kCipherTextGold, TEXT_LENGTH_IN_WORDS);

  // Setup ECB decryption transaction.
  transaction.operation = kDifAesOperationDecrypt;
  CHECK_DIF_OK(dif_aes_start(&aes, &transaction, &key, NULL));

  // Load the previously produced cipher text to start the decryption operation.
  AES_TESTUTILS_WAIT_FOR_STATUS(&aes, kDifAesStatusInputReady, true, TIMEOUT);
  CHECK_DIF_OK(dif_aes_load_data(&aes, out_data_cipher));

  // Read out the produced plain text.
  AES_TESTUTILS_WAIT_FOR_STATUS(&aes, kDifAesStatusOutputValid, true, TIMEOUT);
  dif_aes_data_t out_data_plain;
  CHECK_DIF_OK(dif_aes_read_output(&aes, &out_data_plain));

  // Finish the ECB encryption transaction.
  CHECK_DIF_OK(dif_aes_end(&aes));

  CHECK_ARRAYS_EQ(out_data_plain.data, kPlainText, TEXT_LENGTH_IN_WORDS);

  return true;
}
