# YOLOv4-LibTorch
This is a YOLOv4 post-processing code based on pure libtorch API  

环境：  
CMake 3.13.2  
OpenCV 3.4.10 opencv-contrib-3.4.10  
torchvision-0.6.0  
LibTorch1.5.0-CPU  

用法：  
1、首先将CMakeLists.txt文件中的set(CMAKE_PREFIX_PATH "......")后的路径换成自己环境下的相应值。  
2、将yolo.h文件中的先验框、图片大小、类别三个参数改成自己的相应数据。  
3、主函数是main.cpp,里面有单独的图片检测和摄像头视频检测两部分

说明：
这是CPU版本的，GPU版的看这里：https://github.com/wsx000/YOLOv4-LibTorch-GPU


