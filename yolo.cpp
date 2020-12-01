#include "yolo.h"
#include <ATen/Functions.h>
#include <ATen/core/TensorBody.h>
#include <ATen/core/ivalue.h>
#include <ATen/core/stack.h>
#include <c10/core/ScalarType.h>
#include <opencv2/core/types.hpp>
#include <opencv2/highgui.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/imgproc/types_c.h>
#include <valarray>
#include "torchvision/csrc/nms.h"
#include "chrono"

using namespace std;
using namespace std::chrono;
using namespace torch::indexing;

YOLO::YOLO(const char* modelPath)
{
    // 加载模型
    module = torch::jit::load(modelPath);
    cout << "model load successfully" << endl;
}


// 图片检测
vector<vector<float>> YOLO::detect_image(cv::Mat image)
{
    DecodeBox yolo_decodes1(all_anchors[0], model_image_size, 3, 2);
    DecodeBox yolo_decodes2(all_anchors[1], model_image_size, 3, 2);
    DecodeBox yolo_decodes3(all_anchors[2], model_image_size, 3, 2);

    // 调整图片大小
    cv::Mat crop_img = letterbox_image(image, model_image_size);
    // 调整图片格式
    cv::cvtColor(crop_img, crop_img, CV_BGR2RGB);
    // 转换为tensor
    at::Tensor photo = at::from_blob(crop_img.data, {1, crop_img.rows, crop_img.cols, 3}, at::kByte);
    // 数据类型转换
    photo = photo.toType(at::kFloat);
    // 转换为（b，c，h，w）格式
    photo = photo.permute({0, 3, 1, 2});
    // 归一化
    photo = photo.div_(255.0);

    // 输入
    vector<torch::jit::IValue> input;
    input.emplace_back(photo);

    // 前向传播开始
    auto start = high_resolution_clock::now();
    // 前向传播
    auto outputs = module.forward(input).toTuple();
    auto end = high_resolution_clock::now();
    // 记录模型的推理时间
    auto duration = duration_cast<milliseconds>(end - start);
    cout << "forward takes :" << duration.count() << "ms" << endl;

    // 提取三个特征层的输出
    vector<at::Tensor> out(3);
    out[0] = outputs->elements()[0].toTensor();
    out[1] = outputs->elements()[1].toTensor();
    out[2] = outputs->elements()[2].toTensor();

    // 从输出中提取数据
    vector<at::Tensor> feature_out(3);
    for(size_t i=0; i<3; i++)
    {
        if(i == 0) feature_out[0] = yolo_decodes1.find_obj(out[0]);
        if(i == 1) feature_out[1] = yolo_decodes2.find_obj(out[1]);
        if(i == 2) feature_out[2] = yolo_decodes3.find_obj(out[2]);
    }

    // 在第二维度上做拼接， shape： (bs, 3*(13*13+26*26+52*52)， 5+num_classes)
    at::Tensor output = at::cat({feature_out[0], feature_out[1], feature_out[2]}, 1);

    // 得到nms的输出  输出，类别数，置信度阈值，nms-iou阈值
    vector<at::Tensor> nms_out = yolo_nms(output, 2, 0.5, 0.4);

    // 没有目标，返回为空
    if(nms_out.size() == 0)
    {
        vector<vector<float>> temp;
        return temp;
    }

    // 内容为(x1, y1, x2, y2, obj_conf, class_conf, class_pred)
    at::Tensor bbox = nms_out[0];
    // cout << bbox <<endl;

    // 提取置信度及类别
    at::Tensor top_conf = bbox.index({"...", 4}) * bbox.index({"...", 5}).unsqueeze(-1);  // 提取置信度
    at::Tensor top_label = bbox.index({"...", -1}).unsqueeze(-1);  // 提取所属类别
    // 分别提取定点坐标，并拓展维度
    // at::Tensor top_bbox = bbox.index({"...", Slice(None, 4)});  // 提取坐标
    at::Tensor box_xmin = bbox.index({"...", 0}).unsqueeze(-1);
    at::Tensor box_ymin = bbox.index({"...", 1}).unsqueeze(-1);
    at::Tensor box_xmax = bbox.index({"...", 2}).unsqueeze(-1);
    at::Tensor box_ymax = bbox.index({"...", 3}).unsqueeze(-1);

    // 提取原始图片的宽高
    vector<int> image_shape(2, 0);
    image_shape[0] = image.rows;  // h
    image_shape[1] = image.cols;  // w
    // 恢复到原始的尺寸上
    at::Tensor bboxes = yolo_correct_boxes(box_ymin, box_xmin, box_ymax, box_xmax, at::from_blob(model_image_size, {2}, at::kFloat), at::from_blob(image_shape.data(), {2}, at::kInt).toType(at::kFloat));
    // // 与置信度和类别整合  n * (ymin, xmin, ymax, xmax, conf, class)
    bboxes = at::cat({bboxes, top_conf, top_label}, -1);

    // tensor转换为数组 (n, 6)  
    vector<vector<float>> boxes(bboxes.sizes()[0], vector<float>(6));

    for (int i = 0; i < bboxes.sizes()[0]; i++)
    {
        for (int j = 0; j < 6; j++)
        {
            boxes[i][j] = bboxes.index({at::tensor(i).toType(at::kLong),at::tensor(j).toType(at::kLong)}).item().toFloat();
            // cout << boxes[i][j] << endl;
        }
        
    }
    
    return boxes;
}


// n * (ymin, xmin, ymax, xmax, conf, class)
void YOLO::draw_rectangle(vector<vector<float>> output, cv::Mat img)
{
    int fontFace = cv::FONT_HERSHEY_SIMPLEX;  // 字体
    double fontScale = 0.6;   // 字体大小
    int thickness = 2;  // 粗细
    int baseline;
    // 获取文本框的长度

    for (size_t i = 0; i < output.size(); i++)
    {
        std::cout<<output[i][1]<<output[i][0]<< output[i][3]<< output[i][2] <<std::endl;
        cv::Rect rect(int(output[i][1]), int(output[i][0]), int(output[i][3] - output[i][1]), int(output[i][2] - output[i][0]));
        cv::rectangle(img, rect, cv::Scalar(0, 0, 255), 1, cv::LINE_8,0);
        // 获取文本框的大小
        cv::Size text_size = cv::getTextSize(class_names[int(output[i][5])], fontFace, fontScale, thickness, &baseline);
        cv::Point origin; // 绘制的起点
        origin.x = int(output[i][1]);
        origin.y = int(output[i][0]) + text_size.height;
        // cv::putText(InputOutputArray img, const String &text, Point org, int fontFace, double fontScale, Scalar color)
        cv::putText(img, class_names[int(output[i][5])], origin, fontFace, fontScale, cv::Scalar(0,0,255), thickness);

        // 置信度显示
        string text = to_string(output[i][4]);
        text = text.substr(0, 5);
        cv::Size text_size2 = cv::getTextSize(text, fontFace, fontScale, thickness, &baseline);
        origin.x = origin.x + text_size.width + 3;
        origin.y = int(output[i][0]) + text_size2.height;
        cv::putText(img, text, origin, fontFace, fontScale, cv::Scalar(0,0,255), thickness);

    }

    if(output.size() == 0)
    {
        const string text = "NO OBJ";
        fontScale = 2.0;
        // 获取文本框的大小
        cv::Size text_size = cv::getTextSize(text, fontFace, fontScale, thickness, &baseline);
        cout << "no target detected!" << endl;
        cout << text_size << endl;
        cv::Point origin; // 绘制的起点
        origin.x = 0;
        origin.y = 0 + text_size.height;
        // cv::putText(InputOutputArray img, const String &text, Point org, int fontFace, double fontScale, Scalar color)
        cv::putText(img, text, origin, fontFace, fontScale, cv::Scalar(255,0,0), thickness);
    }

    cv::imshow("hhh", img);
    cv::waitKey(10);
    return ;
}







