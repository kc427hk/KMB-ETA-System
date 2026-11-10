void setup() {
  String chars = "()+,-./0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz–（），．:()-%@ 往分鐘原定預最後班次暫停服務車到暫時沒有預定班次正在同步時間載入中站受阻緩慢尾已過尚未連接先請按進入設定 ";
  
  PFont myFont = createFont("../font/NotoSansHK-Medium.ttf", 28, true, chars.toCharArray());
  
  // 3. 儲存檔案 (加入 try-catch 處理 IOException)
  try {
    myFont.save(createOutput("Noto28.vlw"));
    println("搞掂！字體檔已經成功建立喺: " + sketchPath("Noto28.vlw"));
  } catch (Exception e) {
    println("儲存失敗: " + e.getMessage());
  }
  
  exit();
}