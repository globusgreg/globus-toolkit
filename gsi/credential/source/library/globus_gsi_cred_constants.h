
#ifndef GLOBUS_GSI_CREDENTIAL_CONSTANTS_H
#define GLOBUS_GSI_CREDENTIAL_CONSTANTS_H

/**
 * @defgroup globus_gsi_credential_constants GSI Credential Constants
 */
/**
 * GSI Credential Error codes
 * @ingroup globus_gsi_credential_constants
 */
typedef enum
{
    GLOBUS_GSI_CRED_ERROR_SUCCESS = 0,
    GLOBUS_GSI_CRED_ERROR_READING_PROXY_CRED = 1,
    GLOBUS_GSI_CRED_ERROR_READING_HOST_CRED = 2,
    GLOBUS_GSI_CRED_ERROR_READING_SERVICE_CRED = 3,
    GLOBUS_GSI_CRED_ERROR_READING_CRED = 4,
    GLOBUS_GSI_CRED_ERROR_WRITING_CRED = 5,
    GLOBUS_GSI_CRED_ERROR_WRITING_PROXY_CRED = 6,
    GLOBUS_GSI_CRED_ERROR_CHECKING_PROXY = 7,
    GLOBUS_GSI_CRED_ERROR_VERIFYING_CRED = 8,
    GLOBUS_GSI_CRED_ERROR_WITH_CRED = 9,
    GLOBUS_GSI_CRED_ERROR_WITH_CRED_CERT = 10,
    GLOBUS_GSI_CRED_ERROR_WITH_CRED_PRIVATE_KEY = 11,
    GLOBUS_GSI_CRED_ERROR_WITH_CRED_CERT_CHAIN = 12,
    GLOBUS_GSI_CRED_ERROR_ERRNO = 13,
    GLOBUS_GSI_CRED_ERROR_SYSTEM_CONFIG = 14,
    GLOBUS_GSI_CRED_ERROR_WITH_CRED_HANDLE_ATTRS = 15,
    GLOBUS_GSI_CRED_ERROR_LAST = 16
} globus_gsi_cred_error_t;

/**
 * GSI Credential Type
 * @ingroup globus_gsi_credential_handle
 *
 * An enum representing a GSI Credential Type which holds info about 
 * the type of a particular credential.  The three types of credential
 * can be: GLOBUS_PROXY, GLOBUS_USER, or GLOBUS_HOST.
 * 
 * @see globus_gsi_credential_handle
 */
typedef enum 
{
    GLOBUS_PROXY,
    GLOBUS_USER,
    GLOBUS_HOST,
    GLOBUS_SERVICE,
    GLOBUS_SO_END
} globus_gsi_cred_type_t;

/**
 * Globus Proxy Type Enum
 * @ingroup globus_gsi_proxy
 *
 * SLANG: This enum needs documentation
 */
typedef enum
{
    GLOBUS_ERROR_PROXY = -1,
    GLOBUS_NOT_PROXY = 0,
    GLOBUS_FULL_PROXY = 1,
    GLOBUS_LIMITED_PROXY = 2,
    GLOBUS_RESTRICTED_PROXY = 3
} globus_gsi_cred_proxy_type_t;


#define GLOBUS_NULL_GROUP               "GLOBUS_NULL_GROUP"
#define GLOBUS_NULL_POLICY              "GLOBUS_NULL_POLICY"

#endif

