# Observations

A journal which allows me to make notes on the pipeline structure when learning about it

This should be useful in helping modularise the code and make it more than a 1000+ line monolith main.cpp


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
