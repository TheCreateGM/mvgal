# Research Task 10: Vulkan Layer Development Foundation

This document captures the specific loader and manifest rules required before writing Vulkan-layer code. The old code in this repository attempted to fake Vulkan types and export a large set of entry points without following the real loader contract. That approach was not salvageable. The current implementation replaces it with a minimal loader-compliant layer.

## Loader-Layer Interface

The Khronos loader/layer interface document makes two points that drive the implementation:

- Modern desktop layers negotiate the interface through `vkNegotiateLoaderLayerInterfaceVersion`.
- The loader passes chain information through special `VK_STRUCTURE_TYPE_LOADER_INSTANCE_CREATE_INFO` and `VK_STRUCTURE_TYPE_LOADER_DEVICE_CREATE_INFO` records in `pNext`.

Those records contain the “next” `vkGetInstanceProcAddr` and `vkGetDeviceProcAddr` function pointers that a layer must use to call down the chain. A correct layer therefore does not discover the real driver by `dlopen` guessing and it does not invent opaque fake handles. It extracts the next-layer function pointers, advances the link-info chain, and then calls the next implementation of `vkCreateInstance` or `vkCreateDevice`.

That single design change is why the new code is materially better than the previous version. It follows the documented loader ABI instead of imitating the Vulkan API shape loosely.

## Manifest Format

The loader documentation also matters for manifests. On Linux, implicit layer manifests are discovered in `.../vulkan/implicit_layer.d`, and explicit layer manifests are discovered in `.../vulkan/explicit_layer.d`. The manifest format has a `layer` object with fields such as:

- `name`
- `type`
- `library_path`
- `api_version`
- `implementation_version`
- `description`
- `functions`
- `enable_environment`
- `disable_environment`

The documentation’s manifest history notes that the negotiation function is the key exported function for modern layer discovery. It also documents that manifest version 1.2.1 added `library_arch`, but version 1.0.0 remains a valid base format for ordinary layer manifests. For Milestone 1, an implicit layer with `enable_environment` is the most practical option: the layer is installed in the correct discovery class, but it is only activated when explicitly requested through an environment variable. That matches the user specification better than pretending the layer should always be active globally.

## Dispatch and Object State

The loader documentation’s create-instance and create-device examples imply the central engineering requirement: a layer must maintain dispatch state per instance, per device, and usually per queue or command object it intercepts. For Milestone 1, the repository now tracks:

- instance dispatch tables,
- device dispatch tables,
- queue-to-device associations,
- a global submission counter.

That is enough for a minimal logging/interception layer. It is not yet enough for UMGAL’s long-term goals such as command rewriting, logical-physical device mapping, or external-memory ownership, but it provides the exact place where those future capabilities will attach.

## Why the New Layer Is Intentionally Small

A common mistake in ambitious systems work is exporting dozens of Vulkan functions before the layer has a correct loader contract. That was the state of the previous code. The replacement intentionally does less:

- `vkCreateInstance`
- `vkDestroyInstance`
- `vkCreateDevice`
- `vkDestroyDevice`
- `vkGetDeviceQueue`
- `vkGetDeviceQueue2`
- `vkQueueSubmit`
- `vkQueueSubmit2`
- `vkQueueSubmit2KHR`
- the required loader negotiation and enumeration functions

This narrower surface area is deliberate. A working intercept at `vkQueueSubmit` gives us real telemetry with a real Vulkan ICD. A fake “full layer” gives us nothing.

## Verification Strategy

The correct smoke test is not a compile-only check. The layer should be loaded through the Vulkan loader, pointed at a known ICD, and then exercised by a real queue submission. The test added in this repository does exactly that:

- it points the loader at the build-tree manifest,
- it enables the implicit layer with an environment variable,
- it forces the lavapipe ICD for reproducibility,
- it creates an instance, device, queue, command pool, command buffer, and fence,
- it performs a real `vkQueueSubmit`,
- it verifies that the layer log recorded the submit.

That is the right Milestone 1 validation target. It proves the loader contract, the manifest, the queue interception path, and a real execution path through the Vulkan runtime.

## Sources

- Khronos/LunarG loader-layer interface guide: https://vulkan.lunarg.com/doc/view/1.4.309.0/windows/LoaderLayerInterface.html
- Vulkan Loader architecture overview: https://github.com/KhronosGroup/Vulkan-Loader/blob/main/docs/LoaderInterfaceArchitecture.md
- Local system headers used for implementation:
  - `/usr/include/vulkan/vk_layer.h`
  - `/usr/include/vulkan/vulkan.h`
