# Observations

A journal which allows me to make notes on the pipeline structure when learning about it

This should be useful in helping modularise the code and make it more than a 1000+ line monolith main.cpp

## General Notes

Functions that have a 2/3 at the end simply mean that it supports more functionality than the original functions do, 

but to preserve backwards compatability they marked them as new. The 'pNext' is relatively new for example


## Required Coupling between GLFW Surface and Instance

You create a GLFW window, then a Vulkan Instance, then from both you make a Vulkan Surface

It would seem appropriate that these can be coupled together

### COUPLING
-> `vk::raii::Instance` & `vk::raii::VkSurfaceKHR`


## Physical & Logical Device, AND the Queue

These all seem to be created very closely

It seems appropriate to instantiate all together

### COUPLING
-> `vk::raii::Physical`
-> `vk::raii::Device`
-> `vk::raii::Queue`


## Everything Swap chain should be gathered together

  vk::raii::SwapchainKHR swap_chain = nullptr;
  std::vector<vk::Image> swap_chain_images;
  vk::SurfaceFormatKHR swap_chain_surface_format;
  vk::Extent2D swap_chain_extent;
  std::vector<vk::raii::ImageView> swap_chain_image_views;


## Swap Chain Image View Details

These details are not determined from an existing device, it would make sense for this to be a high level choice 

in an eventual wrapper
