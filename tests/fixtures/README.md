# Synthetic audio fixtures

`synthetic-tone.mp3` is a generated 440 Hz sine wave: 0.2 seconds, 44.1 kHz,
stereo, MP3 128 kbit/s. It contains no factory or third-party sample material.
`tools/modal_mp3_fixture.py` records the exact ffmpeg generation command.
It verifies the manual's MP3 load, embedded-state restore, and playback path.
