void setup() {
  String chars = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz:()-%@ ";
  
  PFont myFont = createFont("Noto Sans HK Medium", 24, true, chars.toCharArray());
  
  // 3. 儲存檔案 (加入 try-catch 處理 IOException)
  try {
    myFont.save(createOutput("Noto24.vlw"));
    println("搞掂！字體檔已經成功建立喺: " + sketchPath("Noto24.vlw"));
  } catch (Exception e) {
    println("儲存失敗: " + e.getMessage());
  }
  
  exit();
}