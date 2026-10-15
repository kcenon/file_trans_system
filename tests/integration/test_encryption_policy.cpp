#include <gtest/gtest.h>
#include <kcenon/file_transfer/client/file_transfer_client.h>
#include <kcenon/file_transfer/server/file_transfer_server.h>
#include <kcenon/file_transfer/server/server_pipeline.h>

namespace kcenon::file_transfer::test {
TEST(EncryptionPolicyTest, InvalidOrMissingSecretsFailBeforeTransfer) {
    EXPECT_FALSE(file_transfer_client::builder().with_encryption(true).build());
    EXPECT_FALSE(file_transfer_client::builder().with_encryption_key(std::vector<std::byte>(31)).build());
    EXPECT_FALSE(file_transfer_server::builder().with_storage_directory(std::filesystem::temp_directory_path()).with_required_encryption().build());
    pipeline_config config; config.enable_encryption = true;
    auto pipeline = server_pipeline::create(config); ASSERT_TRUE(pipeline); EXPECT_FALSE(pipeline.value().start());
}
#ifndef FILE_TRANS_ENABLE_ENCRYPTION
TEST(EncryptionPolicyTest, DisabledCryptoRejectsValidKeyAndPasswordAtPublicBuilders) {
    const std::vector<std::byte> key(32, std::byte{0x42});
    auto keyed = file_transfer_client::builder().with_encryption_key(key).build();
    auto password = file_transfer_client::builder().with_encryption_password("test-secret").build();
    ASSERT_FALSE(keyed); ASSERT_FALSE(password);
    EXPECT_EQ(keyed.error().code, error_code::invalid_configuration);
    EXPECT_EQ(password.error().code, error_code::invalid_configuration);
    for (bool raw_key : {false, true}) {
        auto builder = file_transfer_server::builder()
            .with_storage_directory(std::filesystem::temp_directory_path())
            .with_required_encryption();
        if (raw_key) builder.with_encryption_key(key);
        else builder.with_encryption_password("test-secret");
        auto server = builder.build();
        ASSERT_FALSE(server);
        EXPECT_EQ(server.error().code, error_code::invalid_configuration);
    }
}
#endif
} // namespace kcenon::file_transfer::test
