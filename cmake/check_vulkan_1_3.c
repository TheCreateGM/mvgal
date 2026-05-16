#include <vulkan/vulkan.h>

#ifndef VK_VERSION_1_3
#error "Vulkan 1.3 headers not available"
#endif

int main(void) {
    (void)VK_API_VERSION_1_3;
    return 0;
}
