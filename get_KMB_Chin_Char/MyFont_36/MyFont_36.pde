void setup() {
  String chars = "()+,-./0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz–（），．:()-%@ 往分鐘原定預最後班次暫停服務車到暫時沒有預定班次正在同步時間載入中站 ";
  
  PFont myFont = createFont("../font/NotoSansHK-Medium.ttf", 36, true, chars.toCharArray());
  
  // 3. 儲存檔案 (加入 try-catch 處理 IOException)
  try {
    myFont.save(createOutput("Noto36.vlw"));
    println("搞掂！字體檔已經成功建立喺: " + sketchPath("Noto36.vlw"));
  } catch (Exception e) {
    println("儲存失敗: " + e.getMessage());
  }
  
  exit();
}