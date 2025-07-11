# Hyni bbclass for common functionality

# Default hyni configuration
HYNI_SCHEMA_PATH ??= "${datadir}/hyni/schemas"
HYNI_CONFIG_PATH ??= "${sysconfdir}/hyni"
HYNI_MAX_CACHE_SIZE ??= "50"
HYNI_MAX_MESSAGE_HISTORY ??= "100"
HYNI_DEFAULT_TIMEOUT ??= "30000"

# Feature configuration
HYNI_FEATURES ??= "streaming validation caching"
HYNI_DISABLE_FEATURES ??= "ui websocket"

# Helper function to check if hyni feature is enabled
def hyni_feature_enabled(d, feature):
    features = d.getVar('HYNI_FEATURES', True) or ""
    return feature in features.split()

def hyni_feature_disabled(d, feature):
    disabled = d.getVar('HYNI_DISABLE_FEATURES', True) or ""
    return feature in disabled.split()

# Add hyni-specific CFLAGS
HYNI_CFLAGS = "-DHYNI_SCHEMA_PATH='\"${HYNI_SCHEMA_PATH}\"' -DHYNI_CONFIG_PATH='\"${HYNI_CONFIG_PATH}\"'"
TARGET_CFLAGS:append = " ${HYNI_CFLAGS}"
TARGET_CXXFLAGS:append = " ${HYNI_CFLAGS}"
