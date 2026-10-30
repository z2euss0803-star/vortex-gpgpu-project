/* Host tests can replace this hook; target kernels use their real CPU hint. */
#ifndef cpu_relax
#define cpu_relax() ((void)0)
#endif
