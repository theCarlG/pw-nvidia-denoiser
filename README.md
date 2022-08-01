# Pipewire NVIDIA Denoiser

This is a [Pipewire] Noise Suppression DSP filter using [NVIDIA AudioEffects API], this is just a test and not a
finished product and will only work with 48kHz audio and it's dynamically linked against libraries in the vendor folder
because I'm lazy.

## Building
if you want to build it
you need to add cuda11.3, nvafx and models and then run `make`

```
vendor
├── cuda
│  └── lib
├── models
│  └── sm_86
└── nvafx
   ├── include
   └── lib
```


## Running
Execute the binary with one argument which is the path to a model then you can route the audio through the 
`NVIDIA Denoiser` filter in [Helvum] or some other tool. If possible, configure pipewire to not use power of two quantums
`clock.power-of-two-quantum = false` and set your quantum size to `512`, hopefully pipewire will automatically set
set the quantum to 480 and the denoiser wont use xrun.

```
./build/pw-nvidia-denoiser ./vendor/models/sm_86/denoiser_48k.trtpkg 
```
[NVIDIA AudioEffects API]: https://developer.nvidia.com/maxine-getting-started
[Pipewire]: https://pipewire.org/
[Helvum]: https://gitlab.freedesktop.org/pipewire/helvum
