#include <linux/types.h>

void *____memcpy(void *dest, const void *src, size_t n)
{
	long d0, d1, d2;
	asm volatile("rep ; movsq\n\t"
		     "movq %4,%%rcx\n\t"
		     "rep ; movsb\n\t"
		     : "=&c"(d0), "=&D"(d1), "=&S"(d2)
		     : "0"(n >> 3), "g"(n & 7), "1"(dest), "2"(src)
		     : "memory");

	return dest;
}

inline void *____memcpy_avx(void *dest, const void *src, size_t n)
{
	size_t blocks = n / 256;
	size_t i;
	for (i = 0; i < blocks; i++) {
		asm volatile("vmovdqu %%ymm0, (%1);\n"
			     "vmovdqu %%ymm1, 32(%1);\n"
			     "vmovdqu %%ymm2, 64(%1);\n"
			     "vmovdqu %%ymm3, 96(%1);\n"
			     "vmovdqu %%ymm4, 128(%1);\n"
			     "vmovdqu %%ymm5, 160(%1);\n"
			     "vmovdqu %%ymm6, 192(%1);\n"
			     "vmovdqu %%ymm7, 224(%1);\n"
			     "vmovdqu (%0), %%ymm0;\n"
			     "vmovdqu 32(%0), %%ymm1;\n"
			     "vmovdqu 64(%0), %%ymm2;\n"
			     "vmovdqu 96(%0), %%ymm3;\n"
			     "vmovdqu 128(%0), %%ymm4;\n"
			     "vmovdqu 160(%0), %%ymm5;\n"
			     "vmovdqu 192(%0), %%ymm6;\n"
			     "vmovdqu 224(%0), %%ymm7;\n"
			     :
			     : "r"(dest), "r"(src)
			     : "memory");

		src = (const char *)src + 256;
		dest = (char *)dest + 256;
	}
	return dest;
}

inline void *____memcpy_avx128(void *dest, const void *src, size_t n)
{
	size_t blocks = n / 128;
	size_t i;
	for (i = 0; i < blocks; i++) {
		asm volatile("vmovdqu %%ymm0, (%1);\n"
			     "vmovdqu %%ymm1, 32(%1);\n"
			     "vmovdqu %%ymm2, 64(%1);\n"
			     "vmovdqu %%ymm3, 96(%1);\n"
			     "vmovdqu (%0), %%ymm0;\n"
			     "vmovdqu 32(%0), %%ymm1;\n"
			     "vmovdqu 64(%0), %%ymm2;\n"
			     "vmovdqu 96(%0), %%ymm3;\n"
			     :
			     : "r"(dest), "r"(src)
			     : "memory");

		src = (const char *)src + 128;
		dest = (char *)dest + 128;
	}
	return dest;
}

// void *____memcpy_avx512(void *dest, const void *src, size_t n)
// {
//     size_t blocks = n / 512;
// 	size_t i;
// 	for (i = 0; i < blocks; i++) {
//         asm volatile(
//             "vmovdqu %%ymm0, (%1);\n"
//             "vmovdqu %%ymm1, 32(%1);\n"
//             "vmovdqu %%ymm2, 64(%1);\n"
//             "vmovdqu %%ymm3, 96(%1);\n"
//             "vmovdqu %%ymm0, 128(%1);\n"
//             "vmovdqu %%ymm1, 160(%1);\n"
//             "vmovdqu %%ymm2, 192(%1);\n"
//             "vmovdqu %%ymm3, 224(%1);\n"
//             "vmovdqu %%ymm0, 256(%1);\n"
//             "vmovdqu %%ymm1, 288(%1);\n"
//             "vmovdqu %%ymm2, 320(%1);\n"
//             "vmovdqu %%ymm3, 352(%1);\n"
//             "vmovdqu %%ymm0, 384(%1);\n"
//             "vmovdqu %%ymm1, 416(%1);\n"
//             "vmovdqu %%ymm2, 448(%1);\n"
//             "vmovdqu %%ymm3, 480(%1);\n"
//             "vmovdqu (%0), %%ymm0;\n"
//             "vmovdqu 32(%0), %%ymm1;\n"
//             "vmovdqu 64(%0), %%ymm2;\n"
//             "vmovdqu 96(%0), %%ymm3;\n"
//             "vmovdqu 128(%0), %%ymm0;\n"
//             "vmovdqu 160(%0), %%ymm1;\n"
//             "vmovdqu 192(%0), %%ymm2;\n"
//             "vmovdqu 224(%0), %%ymm3;\n"
//             "vmovdqu 256(%0), %%ymm0;\n"
//             "vmovdqu 288(%0), %%ymm1;\n"
//             "vmovdqu 320(%0), %%ymm2;\n"
//             "vmovdqu 352(%0), %%ymm3;\n"
//             "vmovdqu 384(%0), %%ymm0;\n"
//             "vmovdqu 416(%0), %%ymm1;\n"
//             "vmovdqu 448(%0), %%ymm2;\n"
//             "vmovdqu 480(%0), %%ymm3;\n"
//             :
//             : "r"(dest), "r"(src)
//             : "memory"
//         );

//         src = (const char *)src + 512;
//         dest = (char *)dest + 512;
//     }
// 	return dest;
// }
