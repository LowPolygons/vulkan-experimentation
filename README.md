# Vulkan Pipeline 

This serves less as a README for using the script, and more for documenting my understanding of how it works

# What Happens

- 1) Initialise a Window context
  --> Create a GLFW instance
- 2) Create a Vulkan context and Instance
  --> From the context, evaluate valid layers, extensions, a debugger and create instance
- 3) Create a Surface from the window
  --> Get a window surface from GLFW and make it vulkan applicable with the instance
- 4) Create a Physical Device

## Initialise a Window Context

Vulkan API exists independently from a window API, and so initialise a GLFW context and get the GLFWwindow

GLFW interfaces extremely well with Vulkan, making it a desirable choice

## Create a Vulkan context and Instance

A `vk::raii::Context context` object is initialised, from which stems every object to come 

It sort of acts as a loader for vulkan to *exist*, but not to be used. For that we need an instance

A vulkan instance needs some info:

- ### `App Info`
    -> Application name
    -> Application Version
    -> Engine Name (no engine)
    -> Engine Version
    -> Api Version

Just some boiler plate, really. More crucially, we need:

- ### Layers

    These are components that hook vulkan calls and perform additional operations

    There are some layers, named validation layers, that this program requires 

    I followed the Khronos tutorial, so it needs the 'VK_LAYER_KHRONOS_validation' feature - there are khronos objects used in this version

    The layers that the current context has are loaded, and the required layers are searched for in this list

- ### Extensions

    As the name suggests, these are extensions that the vulkan context MAY offer

    GLFW Has a list of extensions that it requires to interface with vulkan

    The program also makes use of a Debug Messenger - when things go wrong, it knows and prints a message

    Again, it checks that the vulkan context supports all the required extensions

With this information, the `vk::raii::Instance instance` is created 

## Create a Surface from the window

Thanks to GLFW interfacing so well with Vulkan, this is extremely simple. The source code contains a link with more details

Using the previously acquired instance, we create a vulkan surface.

Note: this is a Khronos API `vk::raii::SurfaceKHR surface`
