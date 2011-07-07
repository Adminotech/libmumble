@echo off
md include

xcopy /d /y src\*.h include\
xcopy /d /y Mumble.pb.h include\
