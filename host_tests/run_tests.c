#include "test_main.h"

int g_failures = 0;
int g_checks = 0;

void adpcm_tests(void);
void jbuf_tests(void);
void packet_tests(void);

int main(void)
{
    adpcm_tests();
    jbuf_tests();
    packet_tests();
    printf("\n%d checks, %d failures\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
