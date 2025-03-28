#!/bin/sh
#正点原子@ALIENTEK
#录音前应该设置耳机和扬声器的音量为0，防止干扰
amixer sset 'Headphone Playback ZC' off
#Set the volume of your headphones(98% volume，127 is the MaxVolume)
amixer sset Headphone 0,0
#Turn on the speaker
amixer sset 'Speaker Playback ZC' off
#Set the volume of your Speaker(98% volume，127 is the MaxVolume)
amixer sset Speaker 0,0

#录音
arecord -r 44100 -f S16_LE -c 2 -d 10 record.wav

#再打开耳机和扬声器
amixer sset 'Headphone Playback ZC' on
#Set the volume of your headphones(98% volume，127 is the MaxVolume)
amixer sset Headphone 125,125
#Turn on the speaker
amixer sset 'Speaker Playback ZC' on
#Set the volume of your Speaker(98% volume，127 is the MaxVolume)
amixer sset Speaker 125,125

#播放录音
aplay record.wav