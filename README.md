# ThirdTube

A work-in-progress homebrew YouTube client for the new 3DS  

## Instability Warning

As this app is still in the alpha stage, you may and will encounter crashes and other bugs.  
If you find one of those, it would be helpful to open an issue on this GitHub repository.  

## Description
The video decoding code is taken from [Video player for 3DS by Core-2-Extreme](https://github.com/Core-2-Extreme/Video_player_for_3DS).  
It does not run any javascripts or render html/css, so it's significantly faster than YouTube on the browser.  
The name is derived from the fact that it is the third YouTube client on 3DS, following the official YouTube app (discontinued) and the new 3DS browser.  

## Features

 - 360p Video Playback
   480p might be possible and could be considered in the future development
 - Searching
 - Video suggestion
 - Comments
 - No ads  
   As this app web-scrapes YouTube, it's more like "Ads are not implemented" rather than "We have ad-blocking functionality".  
   Of course, I will never "implement" it :)  

## Screenshots
![](https://github.com/windows-server-2003/ThirdTube/blob/main/screenshots/0.bmp) ![](https://github.com/windows-server-2003/ThirdTube/blob/main/screenshots/1.bmp)  
![](https://github.com/windows-server-2003/ThirdTube/blob/main/screenshots/2.bmp) ![](https://github.com/windows-server-2003/ThirdTube/blob/main/screenshots/3.bmp)  

## Requirments
A New 3DS (including a new 2DS) with [Luma3DS](https://github.com/LumaTeam/Luma3DS) installed and [DSP1](https://github.com/zoogie/DSP1) run.
I haven't tested the minimum system version, but at least 8.1.0-0 is needed.

## Known issues

 - Extracted stream urls randomly return 403 and the video playback fails  
   The reason is unknown, but a temporary workaround is to press Advanced Tab -> Reload when this happens.
 - Stream downloading randomly slows down (about 10% of the times)  
   This is due to YouTube (probably deliberately) throttling the download speed.  
   The workaround is the same as the 403 issue; reloading will fix it.
   Reference : https://github.com/ytdl-org/youtube-dl/issues/29326  
   It looks like the devs on youtube-dl found the solution, and I'm looking into it.  
 - Long comments are cut  
   This is intentionally done for a performance reason and probably fixable by, for example, folding the comments by default.  
 - A bunch of UI and control issues  
   To be improved.  

### Issues that won't be fixed

 - Old 3DS support  
   I'm one who is obsessed about the support of "legacy" devices, but it turned out that old 3DS, without a hardware-decoding capability, cannot even play 144p at a constant 30 FPS.  
   I regret to say that I have no plan to support the old 3DS.  

## FAQs

 - Does it make sense?  
   The **worst** question in the console homebrew scene. Isn't it just exciting to see your favorite videos playing on a 3DS?

## License
You may use this under the terms of the GNU General Public License GPL v3 or under the terms of any later revisions of the GPL. Refer to the provided LICENSE file for further information.

### Credits
* Core 2 Extreme  
  For [Video player for 3DS](https://github.com/Core-2-Extreme/Video_player_for_3DS) which this app is based on.  
  Needless to say, the video playback functionality is essential for this app, and it would not have been possible to develop this software without him spending his time optimizing the code sometimes even with assembly and looking into HW decoding on the new 3DS.
* The contributors of [youtube-dl](https://github.com/ytdl-org/youtube-dl)  
  For YouTube webpage parsing. It was especially helpful for the deobfuscation of ciphered signatures.

