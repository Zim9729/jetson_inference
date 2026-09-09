# Ultralytics YOLO 🚀, AGPL-3.0 license

import argparse
import cv2
import numpy as np
import onnxruntime as ort
import os
import xml.etree.ElementTree as ET
from xml.dom import minidom

# 类外定义类别映射关系，使用字典格式
# CLASS_NAMES = {
#     0: 'class_name1',  # 类别 0 名称
#     1: 'class_name2',  # 类别 1 名称
#     2: 'class_name3'  # 类别 1 名称
#     # 可以添加更多类别...
# }

CLASS_NAMES = {i: f'class_name{i+1}' for i in range(80)}

# 支持的图像扩展名
IMAGE_EXTENSIONS = {'.jpg', '.jpeg', '.png', '.bmp', '.tiff', '.tif', '.webp'}

class YOLO11:
    """YOLO11 目标检测模型类，用于处理推理和可视化。"""

    def __init__(self, onnx_model, confidence_thres, iou_thres,
                 save_img=True, save_xml=True, output_dir=None):
        """
        初始化 YOLO11 类的实例。
        参数：
            onnx_model:      ONNX 模型的路径。
            confidence_thres: 用于过滤检测结果的置信度阈值。
            iou_thres:       非极大值抑制（NMS）的 IoU 阈值。
            save_img:        是否保存标注后的结果图像。
            save_xml:        是否保存 Pascal VOC XML 标签文件（仅有检测结果时保存）。
            output_dir:      结果保存目录。为 None 时与输入图像同目录。
        """
        self.onnx_model = onnx_model
        self.confidence_thres = confidence_thres
        self.iou_thres = iou_thres
        self.save_img = save_img
        self.save_xml = save_xml
        self.output_dir = output_dir

        # 加载类别名称
        self.classes = CLASS_NAMES

        # 为每个类别生成一个颜色调色板
        self.color_palette = np.random.uniform(0, 255, size=(len(self.classes), 3))

        # 推理会话（在 build_session 中初始化）
        self.session = None
        self.model_inputs = None
        self.input_width = None
        self.input_height = None

    def build_session(self):
        """创建 ONNX 推理会话并获取模型输入信息。"""
        self.session = ort.InferenceSession(
            self.onnx_model,
            providers=["CUDAExecutionProvider", "CPUExecutionProvider"]
            if ort.get_device() == "GPU"
            else ["CPUExecutionProvider"],
        )
        print("YOLO11 🚀 目标检测 ONNXRuntime")
        print("模型名称：", self.onnx_model)

        self.model_inputs = self.session.get_inputs()
        input_shape = self.model_inputs[0].shape
        self.input_width = input_shape[2]
        self.input_height = input_shape[3]
        print(f"模型输入尺寸：宽度 = {self.input_width}, 高度 = {self.input_height}")

    def preprocess(self, image_path):
        """
        对输入图像进行预处理，以便进行推理。
        参数：
            image_path: 图像文件路径。
        返回：
            image_data: 经过预处理的图像数据，准备进行推理。
        """
        self.current_image_path = image_path
        self.img = cv2.imread(image_path)
        if self.img is None:
            raise ValueError(f"无法读取图像：{image_path}")
        self.img_height, self.img_width = self.img.shape[:2]

        img = cv2.cvtColor(self.img, cv2.COLOR_BGR2RGB)
        img, self.ratio, (self.dw, self.dh) = self.letterbox(
            img, new_shape=(self.input_width, self.input_height)
        )

        image_data = np.array(img) / 255.0
        image_data = np.transpose(image_data, (2, 0, 1))
        image_data = np.expand_dims(image_data, axis=0).astype(np.float32)
        return image_data

    def letterbox(self, img, new_shape=(640, 640), color=(114, 114, 114), auto=False, scaleFill=False, scaleup=True):
        """
        将图像进行 letterbox 填充，保持纵横比不变，并缩放到指定尺寸。
        """
        shape = img.shape[:2]  # 当前图像的宽高

        if isinstance(new_shape, int):
            new_shape = (new_shape, new_shape)

        # 计算缩放比例
        r = min(new_shape[0] / shape[0], new_shape[1] / shape[1])  # 选择宽高中最小的缩放比
        if not scaleup:  # 仅缩小，不放大
            r = min(r, 1.0)

        # 缩放后的未填充尺寸
        new_unpad = (int(round(shape[1] * r)), int(round(shape[0] * r)))

        # 计算需要的填充
        dw, dh = new_shape[1] - new_unpad[0], new_shape[0] - new_unpad[1]  # 计算填充的尺寸
        dw /= 2  # padding 均分
        dh /= 2

        # 缩放图像
        if shape[::-1] != new_unpad:  # 如果当前图像尺寸不等于 new_unpad，则缩放
            img = cv2.resize(img, new_unpad, interpolation=cv2.INTER_LINEAR)

        # 为图像添加边框以达到目标尺寸
        top, bottom = int(round(dh)), int(round(dh))
        left, right = int(round(dw)), int(round(dw))
        img = cv2.copyMakeBorder(img, top, bottom, left, right, cv2.BORDER_CONSTANT, value=color)

        return img, (r, r), (dw, dh)

    def postprocess(self, input_image, output):
        """
        对模型输出进行后处理，以提取边界框、分数和类别 ID。
        参数：
            input_image (numpy.ndarray): 输入图像。
            output (numpy.ndarray): 模型的输出。
        返回：
            (output_image, has_detections)
        """
        outputs = np.transpose(np.squeeze(output[0]))
        rows = outputs.shape[0]
        boxes, scores, class_ids = [], [], []

        for i in range(rows):
            classes_scores = outputs[i][4:]
            max_score = np.amax(classes_scores)

            if max_score >= self.confidence_thres:
                class_id = np.argmax(classes_scores)
                x, y, w, h = outputs[i][0], outputs[i][1], outputs[i][2], outputs[i][3]

                x -= self.dw
                y -= self.dh
                x /= self.ratio[0]
                y /= self.ratio[1]
                w /= self.ratio[0]
                h /= self.ratio[1]
                left = int(x - w / 2)
                top = int(y - h / 2)
                width = int(w)
                height = int(h)

                boxes.append([left, top, width, height])
                scores.append(max_score)
                class_ids.append(class_id)

        indices = cv2.dnn.NMSBoxes(boxes, scores, self.confidence_thres, self.iou_thres)
        final_boxes, final_scores, final_class_ids = [], [], []
        for i in indices:
            box = boxes[i]
            score = scores[i]
            class_id = class_ids[i]
            self.draw_detections(input_image, box, score, class_id)
            final_boxes.append(box)
            final_scores.append(score)
            final_class_ids.append(class_id)

        has_detections = len(final_boxes) > 0
        print(f"  检测到目标数量：{len(final_boxes)}")

        # 根据开关决定是否保存 XML（仅有检测结果时保存）
        if has_detections and self.save_xml:
            xml_save_path = self._resolve_output_path(self.current_image_path, ".xml")
            self.save_voc_xml(final_boxes, final_scores, final_class_ids, xml_save_path)

        return input_image, has_detections

    def save_voc_xml(self, boxes, scores, class_ids, xml_path):
        """
        将检测结果保存为 Pascal VOC XML 格式。
        参数：
            boxes:    边界框列表，每个元素为 [x, y, w, h]。
            scores:   置信度分数列表。
            class_ids: 类别 ID 列表。
            xml_path: 输出 XML 文件路径。
        """
        root = ET.Element("annotation")

        ET.SubElement(root, "folder").text = os.path.dirname(self.current_image_path)
        ET.SubElement(root, "filename").text = os.path.basename(self.current_image_path)
        ET.SubElement(root, "path").text = self.current_image_path

        source = ET.SubElement(root, "source")
        ET.SubElement(source, "database").text = "Unknown"

        size = ET.SubElement(root, "size")
        ET.SubElement(size, "width").text = str(self.img_width)
        ET.SubElement(size, "height").text = str(self.img_height)
        ET.SubElement(size, "depth").text = "3"

        ET.SubElement(root, "segmented").text = "0"

        for box, score, class_id in zip(boxes, scores, class_ids):
            x, y, w, h = box
            xmin = max(0, int(x))
            ymin = max(0, int(y))
            xmax = min(self.img_width, int(x + w))
            ymax = min(self.img_height, int(y + h))

            obj = ET.SubElement(root, "object")
            ET.SubElement(obj, "name").text = self.classes[class_id]
            ET.SubElement(obj, "pose").text = "Unspecified"
            ET.SubElement(obj, "truncated").text = "0"
            ET.SubElement(obj, "difficult").text = "0"
            ET.SubElement(obj, "confidence").text = f"{score:.4f}"
            bndbox = ET.SubElement(obj, "bndbox")
            ET.SubElement(bndbox, "xmin").text = str(xmin)
            ET.SubElement(bndbox, "ymin").text = str(ymin)
            ET.SubElement(bndbox, "xmax").text = str(xmax)
            ET.SubElement(bndbox, "ymax").text = str(ymax)

        xml_str = minidom.parseString(ET.tostring(root, encoding="unicode")).toprettyxml(indent="    ")
        xml_str = "\n".join(xml_str.split("\n")[1:])

        with open(xml_path, "w", encoding="utf-8") as f:
            f.write(xml_str)
        print(f"  XML 已保存：{xml_path}")

    def draw_detections(self, img, box, score, class_id):
        """
        在输入图像上绘制检测到的边界框和标签。
        参数：
            img: 用于绘制检测结果的输入图像。
            box: 检测到的边界框。
            score: 对应的检测分数。
            class_id: 检测到的目标类别 ID。

        返回：
            None
        """
        # 提取边界框的坐标
        x1, y1, w, h = box

        # 获取类别对应的颜色
        color = self.color_palette[class_id]

        # 在图像上绘制边界框
        cv2.rectangle(img, (int(x1), int(y1)), (int(x1 + w), int(y1 + h)), color, 2)

        # 创建包含类别名和分数的标签文本
        label = f"{self.classes[class_id]}: {score:.2f}"

        # 计算标签文本的尺寸
        (label_width, label_height), _ = cv2.getTextSize(label, cv2.FONT_HERSHEY_SIMPLEX, 0.5, 1)

        # 计算标签文本的位置
        label_x = x1
        label_y = y1 - 10 if y1 - 10 > label_height else y1 + 10

        # 绘制填充的矩形作为标签文本的背景
        cv2.rectangle(img, (label_x, label_y - label_height), (label_x + label_width, label_y + label_height), color,
                      cv2.FILLED)

        # 在图像上绘制标签文本
        cv2.putText(img, label, (label_x, label_y), cv2.FONT_HERSHEY_SIMPLEX, 0.5, (0, 0, 0), 1, cv2.LINE_AA)

    def _resolve_output_path(self, image_path, ext):
        """
        根据 output_dir 设置解析输出文件路径。
        ext: 文件扩展名，如 '_det.jpg' 或 '.xml'
        """
        stem = os.path.splitext(os.path.basename(image_path))[0]
        if self.output_dir:
            os.makedirs(self.output_dir, exist_ok=True)
            return os.path.join(self.output_dir, stem + ext)
        else:
            return os.path.splitext(image_path)[0] + ext

    def detect_single(self, image_path):
        """
        对单张图像执行检测，并根据开关保存结果图和 XML。
        参数：
            image_path: 输入图像路径。
        返回：
            (output_image, has_detections)
        """
        print(f"\n处理图像：{image_path}")
        img_data = self.preprocess(image_path)
        outputs = self.session.run(None, {self.model_inputs[0].name: img_data})
        output_image, has_detections = self.postprocess(self.img, outputs)

        # 根据开关决定是否保存结果图
        if self.save_img:
            img_save_path = self._resolve_output_path(image_path, "_det.jpg")
            cv2.imwrite(img_save_path, output_image)
            print(f"  结果图已保存：{img_save_path}")

        return output_image, has_detections

    def run(self, input_path):
        """
        运行检测。input_path 可以是单张图像路径或图像文件夹路径。
        参数：
            input_path: 图像文件路径 或 文件夹路径。
        """
        self.build_session()

        if os.path.isdir(input_path):
            # 文件夹模式：遍历文件夹内所有图像
            image_files = [
                os.path.join(input_path, f)
                for f in sorted(os.listdir(input_path))
                if os.path.splitext(f)[1].lower() in IMAGE_EXTENSIONS
            ]
            if not image_files:
                print(f"文件夹中未找到图像文件：{input_path}")
                return

            print(f"文件夹模式：共发现 {len(image_files)} 张图像")
            total, detected = 0, 0
            for image_path in image_files:
                try:
                    _, has_detections = self.detect_single(image_path)
                    total += 1
                    if has_detections:
                        detected += 1
                except Exception as e:
                    print(f"  [错误] 跳过 {image_path}：{e}")

            print(f"\n完成！共处理 {total} 张，检测到目标 {detected} 张。")

        elif os.path.isfile(input_path):
            # 单图模式
            output_image, _ = self.detect_single(input_path)
            return output_image

        else:
            raise FileNotFoundError(f"路径不存在：{input_path}")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="YOLO11 ONNX 目标检测")
    parser.add_argument(
        "--model", type=str,
        default=r'E:\02_myproject\0_project\proj2_20260411\config\guang3\guang3-dcqx-best-20260806.onnx',
        help="ONNX 模型路径。",
    )
    parser.add_argument(
        "--input", type=str,
        #default=r'E:\00_MyRail_Data\guang3\G3-FJMN',
        default=r'E:\test_dclf',
        help="输入图像路径 或 图像文件夹路径。",
    )
    parser.add_argument("--conf-thres", type=float, default=0.5, help="置信度阈值")
    parser.add_argument("--iou-thres", type=float, default=0.45, help="NMS IoU 阈值")
    parser.add_argument(
        "--save-img", action="store_true", default=True,
        help="保存标注后的结果图像（默认开启）。",
    )
    parser.add_argument("--no-save-img", dest="save_img", action="store_false",
                        help="不保存结果图像。")
    parser.add_argument(
        "--save-xml", action="store_true", default=True,
        help="保存 Pascal VOC XML 标签文件（默认开启，仅有检测结果时保存）。",
    )
    parser.add_argument("--no-save-xml", dest="save_xml", action="store_false",
                        help="不保存 XML 标签文件。")
    parser.add_argument(
        "--output-dir", type=str, 
        default=r'',
        help="结果保存目录。不指定时与输入图像同目录。",
    )
    args = parser.parse_args()

    detector = YOLO11(
        onnx_model=args.model,
        confidence_thres=args.conf_thres,
        iou_thres=args.iou_thres,
        save_img=args.save_img,
        save_xml=args.save_xml,
        output_dir=args.output_dir,
    )

    detector.run(args.input)
