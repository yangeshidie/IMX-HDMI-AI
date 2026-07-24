#!/bin/bash

OUTPUT_FILE="output_result.txt"
> "$OUTPUT_FILE"

echo "开始生成（安全 CDATA 包裹模式）..."

# 处理 include
if [ -d "include" ]; then
    echo "<head>" >> "$OUTPUT_FILE"
    find include -maxdepth 1 -name "*.h" | sort | while read -r filepath; do
        filename=$(basename "$filepath")
        echo "  <$filename><![CDATA[" >> "$OUTPUT_FILE"
        cat "$filepath" >> "$OUTPUT_FILE"
        echo "  ]]></$filename>" >> "$OUTPUT_FILE"
    done
    echo "</head>" >> "$OUTPUT_FILE"
fi

echo "" >> "$OUTPUT_FILE"

# 处理 src
if [ -d "src" ]; then
    echo "<src>" >> "$OUTPUT_FILE"
    find src -maxdepth 1 -name "*.c" | sort | while read -r filepath; do
        filename=$(basename "$filepath")
        echo "  <$filename><![CDATA[" >> "$OUTPUT_FILE"
        cat "$filepath" >> "$OUTPUT_FILE"
        echo "  ]]></$filename>" >> "$OUTPUT_FILE"
    done
    echo "</src>" >> "$OUTPUT_FILE"
fi

echo "生成完毕！结果已保存至: $OUTPUT_FILE"

# 使用步骤
# chmod +x generate_xml.sh
# ./generate_xml.sh
# cat output_result.txt