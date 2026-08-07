# Vulkan Pipeline 

This serves less as a README for using the script, and more for documenting my understanding of how it works

# What Happens

It is sort of split into 3 categories:

- Software->Hardware Interface Initialisation
- Graphics Pipeline Initialisation
- Command Pipeline initialisation

### Software-Hardware Interface Initialisation
1) Initialise a Window context
   
    -> Create a GLFW instance
   
2) Create a Vulkan context and Instance
   
    -> From the context, evaluate valid layers, extensions, a debugger and create instance
   
3) Create a Surface from the window
   
    -> Get a window surface from GLFW and make it vulkan applicable with the instance
   
4) Create a Physical Device
   
    -> Of the physical devices available, pick one and ensure it supports all requirements
   
5) Create a Logical Device, and a Queue
   
    -> Determine the queue features needed and ensure they are supported

    -> Create an interface between the physical device and the vulkan driver
   
6) Create a Swap Chain
    
    -> Determine the most desirable metadata for the swap chain support by the device and create it
   
7) Create some Image Views
    
    -> Vulkan driver needs to know how to modify each `vk::Image`, this details how


### Graphics Pipeline Initialisation
Note: This is all currently wrapped in the 'createGraphicsPipeline' function


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


## Create a Physical Device

The computer running the code may have multiple physical devices that supports Vulkan

It will try and choose a 'discrete' GPU (as opposed to integrated graphics)

It will then ensure that this GPU supports all the right extensions, api version and queue families

- ### Note on Queues in relation to the physical device

    A queue is where all operations are submitted. Queues exist in families, each with different subsets of features

    This API is restricted to graphics-supported queues. 

    WARN: This may need to change with compute shaders, I'm honestly not sure

    With this, the `vk::raii::PhysicalDevice physical_device` is created

    The question to be answered: Why does it need a Physical AND Logical Device?
    
    A good quote from reddit: "if you have a game that is using vulkan and some other desktop app that uses it as well they both need to function on the same physical device without interferring with each other hence the need for logical devices."

## Create a Logical Device, and a Queue

This is a two stage process

- ### Queue Properties
    The queue is sourced from the logical device, and the queue needs to support the right features for the pipeline. 

    In this case, it needs to support graphics features.

    Queues are split into families, each with different properties.

    The code first gets a list of all the queue family properties fromt he physical device which it supports

    It iterates over this list, and searches for a queue whose queue flags contains `vk::QueueFlagBits::eGraphics`
    and one where the physical device and surface support.

- ### Device Features
    As the logical device is essentially an interface for the driver to access the physical device,

    the logical device needs to know what features to support. The 'Vulkan way' of doing this is a little strange.

    It expects a chain/linked list of features called a 'Structure Chain', and wants a pointer to the first element

With this data, a `vk::raii::Device` can be instantiated, along with a `vk::raii::Queue`

## Create a Swap Chain

First, understanding why the swapchain exists is useful

### What is a swap chain?

  If the OS only provided one image to draw on, throughout the process of drawing the next frame 
  there would be times where part of the screen is the next frame and the rest is the current -> Screen Tearing!
  It also allows the GPU to start rendering the next frame without waiting for the previous to finish displaying

Before a swap chain can be created, it needs to know some details that the physical device can support relating to it

The code gets a list of the following supported details:

### Pre-determinable Details
    -> Surface Capabilties
        --> Metadata such as min/max images allowed in the swap chain
            or the min/max width and height of images in the swapchain (Swap Extent)
    -> Available Surface Formats
        --> This contains information on things like the pixel format or colour space
        --> NOTE: This code expects the BGRA format. If this is not available, it just throws
    -> Present Modes
        --> How the new images are transfered to the screen
            --> Immediate -> no vsync, less latency, potential screen tearing
            --> Fifo -> Standard vsync queue like structure
            --> Fifo Relaxed -> If the app is laggy and the queue is empty during a 'vertical blank', 
                                it just immediatedly displays the immage. Mild stutter/tearing
            --> Mailbox -> Triple-buffering low-latency vsync -> complex

The program then selects the most desirable selection of these

##### Important note on Swap Extend: the swap extent will not necessarily be the screen size

Finally, the `vk::raii::SwapchainKHR` is created.

Note: There is some information that exists closely to the swap chain, but not in the swapchain object.

The program needs (readily):

- Swap Chain
- Swap Chain Images
- Swap Chain Surface Format
- Swap Chain Extent

## Create Image Views

In the swap chain step, the program got a list of `vk::Image` objects corresponding to the images in the swap chain

These are just raw memory chunks with no indication of *how* to draw to them

The image views section specifies how images should be rendered. 

These attributes are Hard Coded, NOT chosen from an existing structure
