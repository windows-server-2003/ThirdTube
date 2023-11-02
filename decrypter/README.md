## YouTube "decrypter" file
YouTube employs some measures for anti-third-party client. Something called "signature cipher" or "n parameter" is one of them.  
We can fetch stream URLs from `ytInitialPlayerResponse` in the video page or `/player` endpoint, but the URLs are unusable or rate-limited without some parameter conversion.  
The conversion is done in `base.js` on the official web client, and the process seems to be always a combination of a certain simple operations.  
We could download `base.js` and execute the conversion code on ThirdTube, but `base.js` is a large(1~2 MB) file and it slows down video loading,
so we make a text file(2-300 B) describing the operations in the conversion process and let the app load it.  
This document describes the format of the file.  

## How ThirdTube loads the file
1. embedded in romfs of the app file (updated with the ThirdTube releases)
2. local cache (updated along with 3.)
3. `decrypter/0_latest.txt` in this repository (latest)

On startup, the app loads the decrypter in the following order, and uses the last one that is succeeded to load.

## How to find conversion function in base.js

### sig
1. Search for `a=a.split("")`. It should be at the start of a function, followed by \[VAR\].\[FUNC\](a,[x]). The function is the conversion function.
2. Search for `var [VAR]=` to see the content of `FUNC`s.

### n
1. Search for `("n")`. The unique match should be followed by `&&(b=[ARR][0](b))`.
2. Go for the defintion of `[ARR]` by searching `var [ARR]` and get the conversion function name

## Format
```
# [STS]
[one or more lines for "sig" conversion]
>
[one or more lines for "n" conversion]
```
`STS` is like a version of the decrypter and seems to be the day count from unix epoch to the deployment of the decrypter.  
A line for "sig"/"n" conversion is one of the following:  
`s [x]` : **s**wap `a[0]` and `a[x mod a.size()]`  
`e [x]` : **e**rase `a[x mod a.size()]` from `a`  
`c [x]` : (**c**ut) erase the first `x` items of `a`  
`r [x]` : **r**otate `a` right for `x mod a.size()` items  
`R` : reverse `a`  
`t [key] [charset]` : **t**ransform `a` with the following procedure:

 1. Replace `A` in `charset` with `ABC...Z` (string containing all latin uppercase letters in order)
 2. Replace `a` in `charset` with `abc...z` (string containing all latin lowercase letters in order)
 3. Replace `0` in `charset` with `012...9` (string containing all digit letters in order)  
    (These three steps are done in order to be able to compress the "decrypter" file)
 4. Let f(c) be the index in `charset` at which the character `c` appears
 5. For $i = 0, 1, 2, ..., \mathrm{a.size()-1}$ do the following :  
     1. Assign `charset[(f(a[i]) - f(key[i])) mod 64]` to `a[i]` (64 is always equal to `charset.size()`)  
     2. Push `a[i]` to the back of `key`  

Here, `a` denotes the original string of "sig"/"n" parameter and `x mod y` denotes an integer between $0$ and $y - 1$ that is congruent to $x$ mod $y$.  
When the url of the streams are fetched, ThirdTube converts the `sig` parameter by executing the lines for `sig` conversion in order and `n` in the same way.  
While YouTube updates the decrypter regularly (around every three days?), we can use old decrypter by requesting the stream with old `STS`.  
However, they seem to invalidate decrypters older than 2-3 weeks, so we have to update the decrypter regularly.  

