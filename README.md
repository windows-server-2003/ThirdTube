# ThirdTube

A work-in-progress homebrew YouTube client for the new 3DS  
[GBAtemp Thread](https://gbatemp.net/threads/release-thirdtube-a-homebrew-youtube-client-for-the-new-3ds.591696/)  
[Discord Server](https://discord.gg/CVcThBCQJM)

## Instability Warning

As this app is still in the alpha stage, you may and will encounter crashes and other bugs.  
If you find one of those, it would be helpful to open an issue on this GitHub repository.  

## Description
It accesses the mobile version of YouTube, parses the important part of the downloaded html and plays the stream using the decoder taken from [Video player for 3DS by Core-2-Extreme](https://github.com/Core-2-Extreme/Video_player_for_3DS).  
It does not run any javascripts or render html/css, so it's significantly faster than YouTube on the browser.  
The name is derived from the fact that it is the third YouTube client on 3DS, following the official YouTube app (discontinued) and the new 3DS browser.  

## Screenshots
![](https://github.com/windows-server-2003/ThirdTube/blob/main/screenshots/0.bmp) ![](https://github.com/windows-server-2003/ThirdTube/blob/main/screenshots/1.bmp)  
![](https://github.com/windows-server-2003/ThirdTube/blob/main/screenshots/2.bmp) ![](https://github.com/windows-server-2003/ThirdTube/blob/main/screenshots/3.bmp)  

## Features

 - Video Playback up to 360p  
   480p might be possible and could be considered in the future development
 - Livestreams and premiere videos support
 - Searching  
 - Video suggestion  
 - Comments  
 - Captions  
 - Local watch history and channel subscription  
 - No ads  
   As this app web-scrapes YouTube, it's more like "Ads are not implemented" rather than "We have ad-blocking functionality".  
   Of course, I will never "implement" it :)  

## Controls

 - B button : go back to the previous scene  
 - D-pad up/down : scroll
 - In video player
    - Arrow left/right : 10 s seek


## Requirments
A New 3DS (including a new 2DS) with [Luma3DS](https://github.com/LumaTeam/Luma3DS) installed and [DSP1](https://github.com/zoogie/DSP1) run.  
I haven't tested the minimum system version, but at least 8.1.0-0 is needed.  

### Issues that won't be fixed

 - Old 3DS support  
   I'm one who is obsessed about the support of "legacy" devices, but it turned out that old 3DS, without a hardware-decoding capability, cannot even play 144p at a constant 30 FPS.  
   I regret to say that I have no plan to support the old 3DS.  

## FAQs

 - Does it make sense?  
   The **worst** question in the console homebrew scene. Isn't it just exciting to see your favorite videos playing on a 3DS?

## License
You can use the code under the terms of the GNU General Public License GPL v3 or under the terms of any later revisions of the GPL. Refer to the provided LICENSE file for further information.

## Third-part licenses

### [FFmpeg](https://ffmpeg.org/)
by the FFmpeg developers under GNU Lesser General Public License (LGPL) version 2.1  
The source code can be found in library/FFmpeg/FFmpeg.  
### [json11](https://github.com/dropbox/json11)
by Dropbox under MIT License  
### [libctru](https://github.com/devkitPro/libctru)
by devkitPro under zlib License  
### [libcurl](https://curl.se/)
by Daniel Stenberg and many contributors under the curl License  
### [stb](https://github.com/nothings/stb/)
by Sean Barrett under MIT License and Public Domain  

## Credits
* Core 2 Extreme  
  For [Video player for 3DS](https://github.com/Core-2-Extreme/Video_player_for_3DS) which this app is based on.  
  Needless to say, the video playback functionality is essential for this app, and it would not have been possible to develop this software without him spending his time optimizing the code sometimes even with assembly and looking into HW decoding on the new 3DS.
* dixy52-beep  
  For in-app textures
* [PokéTube](https://github.com/Poketubepoggu)  
  For the icon and the banner
* The contributors of [youtube-dl](https://github.com/ytdl-org/youtube-dl)  
  As a reference about YouTube webpage parsing. It was especially helpful for the deobfuscation of ciphered signatures.  
* The contributors of [pytube](https://github.com/pytube/pytube)  
  As a reference about YouTube webpage parsing. Thanks to its strict dependency-free policy, I was able to port some of the code without difficulty.  

