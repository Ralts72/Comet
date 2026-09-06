layout(push_constant) uniform Parameters {
    float exposure;
    uint encode_srgb;
    float bloom_strength;
    float bloom_threshold;
    uint mode;
} parameters;
