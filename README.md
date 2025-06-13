Videomaster Fork of FFmpeg README
=================================

FFmpeg is a collection of libraries and tools for processing multimedia content
such as audio, video, subtitles, and related metadata.

## About This Fork

This repository is a fork of [FFmpeg](https://ffmpeg.org/) with a custom demuxer added to support DELTACAST (c) PCIe capture cards via the VideoMaster SDK.
The main addition is the `videomaster` input device, which allows FFmpeg to capture video and audio streams directly from supported DELTACAST hardware.

### Building with videomaster Support

To enable the `videomaster` demuxer, configure FFmpeg with the `--enable-videomaster` option in the configure command line.

You must also have the DELTACAST VideoMaster SDK installed and provide the appropriate `--extra-c[xx]flags` to specify the path to the SDK's public headers, and `--extra-ldflags` to specify the path to the SDK libraries.

An example configure command line is:

```sh
configure --enable-gpl --enable-nonfree --enable-videomaster --extra-cflags="-I../../install/videomaster/win64/include" --extra-cxxflags="-I../../install/videomaster/win64/include" --extra-ldflags="-LIBPATH:../../install/videomaster/win64/lib"
```

### Usage

Once built, you can use the `videomaster` input device in FFmpeg commands to capture from DELTACAST cards.
For example:

```sh
ffmpeg -f videomaster -i "stream 0 on board 0" output.avi
```

See the [Input Devices documentation](doc/indevs.texi) for detailed usage and options.

### Documentation

Documentation for the `videomaster` demuxer is included in `doc/indevs.texi` and will be available in the compiled documentation.

## Libraries

* `libavcodec` provides implementation of a wider range of codecs.
* `libavformat` implements streaming protocols, container formats and basic I/O access.
* `libavutil` includes hashers, decompressors and miscellaneous utility functions.
* `libavfilter` provides means to alter decoded audio and video through a directed graph of connected filters.
* `libavdevice` provides an abstraction to access capture and playback devices.
* `libswresample` implements audio mixing and resampling routines.
* `libswscale` implements color conversion and scaling routines.

## Tools

* [ffmpeg](https://ffmpeg.org/ffmpeg.html) is a command line toolbox to
  manipulate, convert and stream multimedia content.
* [ffplay](https://ffmpeg.org/ffplay.html) is a minimalistic multimedia player.
* [ffprobe](https://ffmpeg.org/ffprobe.html) is a simple analysis tool to inspect
  multimedia content.
* Additional small tools such as `aviocat`, `ismindex` and `qt-faststart`.

## Documentation

The offline documentation is available in the **doc/** directory.

The online documentation is available in the main [website](https://ffmpeg.org)
and in the [wiki](https://trac.ffmpeg.org).

### Examples

Coding examples are available in the **doc/examples** directory.

## License

FFmpeg codebase is mainly LGPL-licensed with optional components licensed under
GPL. Please refer to the LICENSE file for detailed information.

## Contributing

Patches should be submitted to the ffmpeg-devel mailing list using
`git format-patch` or `git send-email`. Github pull requests should be
avoided because they are not part of our review process and will be ignored.
