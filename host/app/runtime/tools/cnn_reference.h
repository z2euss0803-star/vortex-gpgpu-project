#ifndef CNN_REFERENCE_H
#define CNN_REFERENCE_H
#include <cstdint>
struct CnnWeights { int32_t conv[18], conv_bias[2], fc[36], fc_bias[2]; };
struct CnnOutputs { int32_t z[72], r[72], p[18], y[2]; };
int cnn_reference(const int32_t input[64], const CnnWeights&, CnnOutputs*);
#endif
