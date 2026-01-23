// BSD 3-Clause License
//
// Copyright (c) 2025, kcenon
// All rights reserved.

/**
 * @file feature_flags.h
 * @brief Unified feature flags header for file_trans_system
 *
 * This is the central entry point for all feature detection and integration
 * flags in the file_trans_system library. Include this header to get access
 * to all FILE_TRANS_HAS_* and KCENON_WITH_* feature macros.
 *
 * Feature categories:
 * - FILE_TRANS_HAS_*     : Local feature availability (LZ4, encryption, etc.)
 * - KCENON_WITH_*        : System integration flags (inherited from common_system)
 *
 * Usage:
 * @code
 * #include <kcenon/file_transfer/config/feature_flags.h>
 *
 * #if FILE_TRANS_HAS_LZ4
 *     compress_with_lz4(data);
 * #endif
 *
 * #if KCENON_WITH_LOGGER_SYSTEM
 *     logger_->log(level, message);
 * #endif
 * @endcode
 *
 * @see common_system/config/feature_flags.h for upstream feature detection
 */

#pragma once

//==============================================================================
// Include common_system feature flags if available
//==============================================================================

#if __has_include(<kcenon/common/config/feature_flags.h>)
#include <kcenon/common/config/feature_flags.h>
#define FILE_TRANS_HAS_COMMON_FEATURE_FLAGS 1
#else
#define FILE_TRANS_HAS_COMMON_FEATURE_FLAGS 0
#endif

//==============================================================================
// File Transfer System Feature Flags
//==============================================================================

/**
 * @brief LZ4 Compression Support
 *
 * When enabled, file_trans_system can compress data using LZ4 algorithm.
 * This is typically set via CMake option FILE_TRANS_ENABLE_LZ4.
 */
#ifndef FILE_TRANS_HAS_LZ4
    #if defined(FILE_TRANS_ENABLE_LZ4)
        #define FILE_TRANS_HAS_LZ4 1
    #else
        #define FILE_TRANS_HAS_LZ4 0
    #endif
#endif

/**
 * @brief Encryption Support (OpenSSL)
 *
 * When enabled, file_trans_system provides AES-GCM encryption capabilities.
 * Requires OpenSSL libraries. Set via CMake option FILE_TRANS_ENABLE_ENCRYPTION.
 */
#ifndef FILE_TRANS_HAS_ENCRYPTION
    #if defined(FILE_TRANS_ENABLE_ENCRYPTION)
        #define FILE_TRANS_HAS_ENCRYPTION 1
    #else
        #define FILE_TRANS_HAS_ENCRYPTION 0
    #endif
#endif

/**
 * @brief Cloud Storage Support
 *
 * When enabled, file_trans_system can interact with cloud storage providers
 * (S3, Azure Blob, GCS). Requires encryption support for request signing.
 */
#ifndef FILE_TRANS_HAS_CLOUD_STORAGE
    #if FILE_TRANS_HAS_ENCRYPTION
        #define FILE_TRANS_HAS_CLOUD_STORAGE 1
    #else
        #define FILE_TRANS_HAS_CLOUD_STORAGE 0
    #endif
#endif

//==============================================================================
// System Integration Flags
//==============================================================================

/**
 * @brief Ensure KCENON_WITH_* flags are always available
 *
 * These flags indicate integration with other kcenon ecosystem modules.
 * They are typically set via CMake compile definitions and may be inherited
 * from common_system's feature_flags.h.
 *
 * When common_system feature_flags.h is available, these are already defined.
 * We only provide defaults here for standalone usage scenarios.
 */

// common_system integration (always available when feature_flags.h is included)
#ifndef KCENON_WITH_COMMON_SYSTEM
    #define KCENON_WITH_COMMON_SYSTEM 0
#endif

// thread_system integration (typed_thread_pool for pipeline)
#ifndef KCENON_WITH_THREAD_SYSTEM
    #if defined(BUILD_WITH_THREAD_SYSTEM)
        #define KCENON_WITH_THREAD_SYSTEM 1
    #else
        #define KCENON_WITH_THREAD_SYSTEM 0
    #endif
#endif

// logger_system integration (structured logging)
#ifndef KCENON_WITH_LOGGER_SYSTEM
    #if defined(BUILD_WITH_LOGGER_SYSTEM)
        #define KCENON_WITH_LOGGER_SYSTEM 1
    #else
        #define KCENON_WITH_LOGGER_SYSTEM 0
    #endif
#endif

// network_system integration (TCP/TLS transport layer)
#ifndef KCENON_WITH_NETWORK_SYSTEM
    #if defined(BUILD_WITH_NETWORK_SYSTEM)
        #define KCENON_WITH_NETWORK_SYSTEM 1
    #else
        #define KCENON_WITH_NETWORK_SYSTEM 0
    #endif
#endif

// container_system integration (bounded_queue for backpressure)
#ifndef KCENON_WITH_CONTAINER_SYSTEM
    #if defined(BUILD_WITH_CONTAINER_SYSTEM)
        #define KCENON_WITH_CONTAINER_SYSTEM 1
    #else
        #define KCENON_WITH_CONTAINER_SYSTEM 0
    #endif
#endif

// monitoring_system integration (metrics and health checks)
#ifndef KCENON_WITH_MONITORING_SYSTEM
    #if defined(BUILD_WITH_MONITORING_SYSTEM)
        #define KCENON_WITH_MONITORING_SYSTEM 1
    #else
        #define KCENON_WITH_MONITORING_SYSTEM 0
    #endif
#endif

//==============================================================================
// Logger System Integration Helper
//==============================================================================

/**
 * @brief Unified flag for logger_system usage in file_transfer
 *
 * This macro indicates whether the logger_system integration is active.
 * It considers both the KCENON_WITH_LOGGER_SYSTEM flag and the legacy
 * BUILD_WITH_LOGGER_SYSTEM macro for backward compatibility.
 */
#ifndef FILE_TRANSFER_USE_LOGGER_SYSTEM
    #if KCENON_WITH_LOGGER_SYSTEM && KCENON_WITH_COMMON_SYSTEM
        #define FILE_TRANSFER_USE_LOGGER_SYSTEM 1
    #elif defined(BUILD_WITH_LOGGER_SYSTEM) && defined(BUILD_WITH_COMMON_SYSTEM)
        #define FILE_TRANSFER_USE_LOGGER_SYSTEM 1
    #else
        #define FILE_TRANSFER_USE_LOGGER_SYSTEM 0
    #endif
#endif

//==============================================================================
// Legacy Alias Support (for backward compatibility)
//==============================================================================

/**
 * @brief Legacy BUILD_WITH_* macro aliases
 *
 * These aliases map KCENON_WITH_* flags back to BUILD_WITH_* macros
 * for backward compatibility with code that uses the old naming convention.
 *
 * @note Legacy aliases are planned for deprecation in a future version.
 *       New code should use KCENON_WITH_* macros directly.
 */
#ifndef FILE_TRANS_DISABLE_LEGACY_ALIASES

// BUILD_WITH_COMMON_SYSTEM <- KCENON_WITH_COMMON_SYSTEM
#ifndef BUILD_WITH_COMMON_SYSTEM
    #if KCENON_WITH_COMMON_SYSTEM
        #define BUILD_WITH_COMMON_SYSTEM 1
    #endif
#endif

// BUILD_WITH_THREAD_SYSTEM <- KCENON_WITH_THREAD_SYSTEM
#ifndef BUILD_WITH_THREAD_SYSTEM
    #if KCENON_WITH_THREAD_SYSTEM
        #define BUILD_WITH_THREAD_SYSTEM 1
    #endif
#endif

// BUILD_WITH_LOGGER_SYSTEM <- KCENON_WITH_LOGGER_SYSTEM
#ifndef BUILD_WITH_LOGGER_SYSTEM
    #if KCENON_WITH_LOGGER_SYSTEM
        #define BUILD_WITH_LOGGER_SYSTEM 1
    #endif
#endif

// BUILD_WITH_NETWORK_SYSTEM <- KCENON_WITH_NETWORK_SYSTEM
#ifndef BUILD_WITH_NETWORK_SYSTEM
    #if KCENON_WITH_NETWORK_SYSTEM
        #define BUILD_WITH_NETWORK_SYSTEM 1
    #endif
#endif

// BUILD_WITH_CONTAINER_SYSTEM <- KCENON_WITH_CONTAINER_SYSTEM
#ifndef BUILD_WITH_CONTAINER_SYSTEM
    #if KCENON_WITH_CONTAINER_SYSTEM
        #define BUILD_WITH_CONTAINER_SYSTEM 1
    #endif
#endif

#endif // FILE_TRANS_DISABLE_LEGACY_ALIASES

//==============================================================================
// Feature Summary (for debugging)
//==============================================================================

/**
 * @brief Print feature detection summary at compile time
 *
 * Enable by defining FILE_TRANS_PRINT_FEATURE_SUMMARY before including this
 * header. Useful for debugging feature detection issues.
 */
#ifdef FILE_TRANS_PRINT_FEATURE_SUMMARY

#pragma message("=== File Transfer System Feature Summary ===")

#if FILE_TRANS_HAS_LZ4
    #pragma message("  LZ4 Compression: Enabled")
#else
    #pragma message("  LZ4 Compression: Disabled")
#endif

#if FILE_TRANS_HAS_ENCRYPTION
    #pragma message("  Encryption (OpenSSL): Enabled")
#else
    #pragma message("  Encryption (OpenSSL): Disabled")
#endif

#if FILE_TRANS_HAS_CLOUD_STORAGE
    #pragma message("  Cloud Storage: Enabled")
#else
    #pragma message("  Cloud Storage: Disabled")
#endif

#pragma message("--- System Integration ---")

#if KCENON_WITH_COMMON_SYSTEM
    #pragma message("  common_system: Available")
#else
    #pragma message("  common_system: Not Available")
#endif

#if KCENON_WITH_THREAD_SYSTEM
    #pragma message("  thread_system: Available")
#else
    #pragma message("  thread_system: Not Available")
#endif

#if KCENON_WITH_LOGGER_SYSTEM
    #pragma message("  logger_system: Available")
#else
    #pragma message("  logger_system: Not Available")
#endif

#if KCENON_WITH_NETWORK_SYSTEM
    #pragma message("  network_system: Available")
#else
    #pragma message("  network_system: Not Available")
#endif

#if KCENON_WITH_CONTAINER_SYSTEM
    #pragma message("  container_system: Available")
#else
    #pragma message("  container_system: Not Available")
#endif

#pragma message("=============================================")

#endif // FILE_TRANS_PRINT_FEATURE_SUMMARY
