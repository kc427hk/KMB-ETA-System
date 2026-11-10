void setup() {
  // 1. 將你要嘅字全部塞入呢個 String (包埋英數符號)
  String chars = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz:()-%@ ";
  
  // 2. 建立字體
  PFont myFont = createFont("Noto Sans HK Medium", 16, true, chars.toCharArray());
  
  // 3. 儲存檔案 (加入 try-catch 處理 IOException)
  try {
    myFont.save(createOutput("Noto16.vlw"));
    println("搞掂！字體檔已經成功建立喺: " + sketchPath("Noto16.vlw"));
  } catch (Exception e) {
    println("儲存失敗: " + e.getMessage());
  }
  
  exit();
}