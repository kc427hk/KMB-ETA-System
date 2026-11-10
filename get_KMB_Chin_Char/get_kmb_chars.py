import json
import urllib.request

def fetch_kmb_stops():
    print("正在從九巴 Open API 抓取全港站點資料...")
    url = "https://data.etabus.gov.hk/v1/transport/kmb/stop"
    
    try:
        req = urllib.request.Request(
            url, 
            headers={'User-Agent': 'Mozilla/5.0'}
        )
        with urllib.request.urlopen(req) as response:
            res_data = json.loads(response.read().decode('utf-8'))
            return res_data.get('data', [])
    except Exception as e:
        print(f"抓取失敗: {e}")
        return []

def extract_unique_characters(stops):
    unique_chars = set()
    
    # 加入常用英文字母、數字、標點及括號
    for char in "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789 ()（）/-.":
        unique_chars.add(char)
        
    for stop in stops:
        # 提取中文站名 (name_tc)
        name_tc = stop.get('name_tc', '')
        for char in name_tc:
            unique_chars.add(char)
            
    # 順序排序輸出
    sorted_chars = sorted(list(unique_chars))
    return "".join(sorted_chars)

if __name__ == "__main__":
    stops = fetch_kmb_stops()
    if stops:
        print(f"成功獲取 {len(stops)} 個巴士站資料。")
        chars = extract_unique_characters(stops)
        print(f"過濾後，全港巴士站總共包含不重複字符數量: {len(chars)} 個")
        print("\n--- 提取出來的字元清單預覽 ---")
        print(chars)
        
        # 儲存成文字檔備用
        with open("kmb_unique_chars.txt", "w", encoding="utf-8") as f:
            f.write(chars)
        print("\n已成功將字元清單儲存至 `kmb_unique_chars.txt` 檔案！")