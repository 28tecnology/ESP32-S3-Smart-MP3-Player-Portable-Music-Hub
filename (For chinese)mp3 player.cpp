#include <Arduino.h>
#include <SPI.h>
#include <SD.h>
#include <TFT_eSPI.h>
#include <VS1053.h>
#include <FS.h>
#include <vector>
#include <string>

// 硬件定义
#define TFT_CS        5
#define TFT_DC        2
#define TFT_RST       -1  // 连接到RST引脚
#define VS1053_CS     14
#define VS1053_DCS    13
#define VS1053_DREQ   15
#define SD_CS         10
#define ROTARY_CLK    18
#define ROTARY_DT     19
#define ROTARY_SW     23
#define PLAY_PAUSE_SW 25
#define NEXT_SW       26
#define PREV_SW       27

// 实例化外设对象
TFT_eSPI tft = TFT_eSPI();
VS1053 player(VS1053_CS, VS1053_DCS, VS1053_DREQ);

// 播放状态
bool isPlaying = false;
bool isPaused = false;
int currentTrack = 0;
int volume = 20;  // 0-100
std::vector<String> currentItems;  // 当前目录下的所有项（文件和文件夹）
std::vector<bool> isDirectory;     // 标记每个项是否为文件夹
String currentPath = "/";          // 当前浏览的路径
std::vector<String> pathHistory;   // 路径历史，用于返回上级目录

// 编码器状态
int lastEncoderPos = 0;
int currentEncoderPos = 0;
int encoderDirection = 0;
bool encoderButtonPressed = false;
bool playPauseButtonPressed = false;
bool nextButtonPressed = false;
bool prevButtonPressed = false;

// 界面状态
enum class MenuState {
  FILE_BROWSER,
  PLAYING,
  VOLUME_ADJUST
};
MenuState currentState = MenuState::FILE_BROWSER;

// 初始化屏幕
void initDisplay() {
  tft.init();
  tft.setRotation(1);
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextSize(1);
  tft.setCursor(0, 0);
  tft.println("ESP32 MP3 Player");
}

// 初始化VS1053解码器
void initPlayer() {
  player.begin();
  player.switchToMp3Mode();
  player.setVolume(volume);
}

// 初始化SD卡
bool initSDCard() {
  if (!SD.begin(SD_CS, SPI, 4000000)) {
    tft.fillScreen(TFT_BLACK);
    tft.setCursor(0, 0);
    tft.println("SD卡初始化失败!");
    return false;
  }
  return true;
}

// 扫描当前目录下的文件和文件夹
void scanCurrentDirectory() {
  tft.fillScreen(TFT_BLACK);
  tft.setCursor(0, 0);
  tft.print("扫描目录: ");
  tft.println(currentPath);
  
  File dir = SD.open(currentPath);
  if (!dir) {
    tft.println("无法打开目录!");
    return;
  }
  
  if (!dir.isDirectory()) {
    dir.close();
    return;
  }
  
  currentItems.clear();
  isDirectory.clear();
  
  // 如果不是根目录，添加".."用于返回上级目录
  if (currentPath != "/") {
    currentItems.push_back("..");
    isDirectory.push_back(true);
  }
  
  // 扫描目录中的所有项
  while (true) {
    File entry = dir.openNextFile();
    if (!entry) break;
    
    String name = entry.name();
    // 提取文件名（去除路径前缀）
    int lastSlash = name.lastIndexOf('/');
    if (lastSlash >= 0) {
      name = name.substring(lastSlash + 1);
    }
    
    if (entry.isDirectory()) {
      currentItems.push_back(name);
      isDirectory.push_back(true);
    } else {
      // 只添加MP3文件
      if (name.endsWith(".mp3") || name.endsWith(".MP3")) {
        currentItems.push_back(name);
        isDirectory.push_back(false);
      }
    }
    entry.close();
  }
  
  dir.close();
  
  tft.fillScreen(TFT_BLACK);
  tft.setCursor(0, 0);
  tft.print("找到 ");
  tft.print(currentItems.size());
  tft.println(" 个项目");
  delay(500);
}

// 绘制文件浏览器界面
void drawFileBrowser() {
  tft.fillScreen(TFT_BLACK);
  tft.setTextSize(1);
  
  // 显示当前路径
  tft.setCursor(0, 0);
  tft.setTextColor(TFT_YELLOW, TFT_BLACK);
  tft.print("路径: ");
  tft.println(currentPath);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.println("----------------");
  
  int displayStart = max(0, currentEncoderPos - 3);
  int displayEnd = min((int)currentItems.size() - 1, displayStart + 6);
  
  for (int i = displayStart; i <= displayEnd; i++) {
    if (i == currentEncoderPos) {
      tft.setTextColor(TFT_BLACK, TFT_WHITE);
    } else {
      tft.setTextColor(TFT_WHITE, TFT_BLACK);
    }
    
    String displayName = currentItems[i];
    
    // 为文件夹添加特殊标记
    if (isDirectory[i]) {
      displayName += "/";
    }
    
    // 截断过长的名称
    if (displayName.length() > 18) {
      displayName = displayName.substring(0, 15) + "...";
    }
    
    tft.println(displayName);
  }
}

// 绘制播放界面
void drawPlayingScreen() {
  tft.fillScreen(TFT_BLACK);
  tft.setTextSize(1);
  
  // 显示歌曲信息
  tft.setCursor(0, 0);
  tft.println("正在播放:");
  
  String songName = currentItems[currentTrack];
  if (songName.length() > 20) {
    songName = songName.substring(0, 17) + "...";
  }
  tft.setTextSize(2);
  tft.println(songName);
  tft.setTextSize(1);
  
  // 显示路径信息
  tft.setTextColor(TFT_YELLOW);
  tft.print("路径: ");
  tft.println(currentPath);
  tft.setTextColor(TFT_WHITE);
  
  // 显示进度条
  tft.println("----------------");
  tft.fillRect(10, 50, 140, 10, TFT_GRAY);
  int progressWidth = map(player.getPositionMS(), 0, player.getDurationMS(), 0, 140);
  tft.fillRect(10, 50, progressWidth, 10, TFT_GREEN);
  
  // 显示时间
  int currentSec = player.getPositionMS() / 1000;
  int totalSec = player.getDurationMS() / 1000;
  tft.setCursor(10, 65);
  tft.print(currentSec / 60);
  tft.print(":");
  tft.print(currentSec % 60);
  tft.print(" / ");
  tft.print(totalSec / 60);
  tft.print(":");
  tft.println(totalSec % 60);
  
  // 显示播放状态
  tft.print("状态: ");
  if (isPaused) {
    tft.println("已暂停");
  } else if (isPlaying) {
    tft.println("播放中");
  } else {
    tft.println("已停止");
  }
  
  // 显示音量
  tft.println("----------------");
  tft.print("音量: ");
  tft.print(volume);
  tft.print("/100 ");
  
  for (int i = 0; i < volume / 10; i++) {
    tft.print("|");
  }
}

// 绘制音量调节界面
void drawVolumeScreen() {
  tft.fillScreen(TFT_BLACK);
  tft.setTextSize(2);
  tft.setCursor(0, 30);
  tft.print("音量: ");
  tft.print(volume);
  tft.println("/100");
  
  tft.setTextSize(1);
  tft.fillRect(10, 60, 140, 20, TFT_GRAY);
  int volumeWidth = map(volume, 0, 100, 0, 140);
  tft.fillRect(10, 60, volumeWidth, 20, TFT_BLUE);
}

// 播放选定的歌曲
void playSelectedTrack() {
  if (currentItems.empty()) return;
  
  player.stopSong();
  isPlaying = true;
  isPaused = false;
  
  // 构建完整文件路径
  String filePath = currentPath + currentItems[currentTrack];
  
  File file = SD.open(filePath);
  
  if (!file) {
    tft.fillScreen(TFT_BLACK);
    tft.setCursor(0, 0);
    tft.print("无法打开文件: ");
    tft.println(filePath);
    delay(2000);
    currentState = MenuState::FILE_BROWSER;
    drawFileBrowser();
    return;
  }
  
  player.startSong();
  while (file.available() && isPlaying) {
    if (player.data_request()) {
      uint8_t buffer[32];
      size_t bytesRead = file.read(buffer, 32);
      player.playChunk(buffer, bytesRead);
    }
    
    // 检查按键状态
    updateButtonStates();
    
    // 处理播放控制
    if (playPauseButtonPressed || encoderButtonPressed) {
      togglePlayPause();
      encoderButtonPressed = false;
      playPauseButtonPressed = false;
    }
    
    if (nextButtonPressed) {
      nextTrack();
      nextButtonPressed = false;
      break;
    }
    
    if (prevButtonPressed) {
      prevTrack();
      prevButtonPressed = false;
      break;
    }
    
    // 处理编码器旋转
    if (encoderDirection != 0) {
      if (encoderDirection > 0) {
        volume = min(100, volume + 2);
      } else {
        volume = max(0, volume - 2);
      }
      player.setVolume(volume);
      encoderDirection = 0;
    }
    
    // 更新显示
    if (millis() % 500 == 0) {
      drawPlayingScreen();
    }
  }
  
  file.close();
  
  // 如果不是手动停止，自动播放下一曲
  if (isPlaying) {
    nextTrack();
  }
}

// 切换播放/暂停状态
void togglePlayPause() {
  if (isPlaying) {
    if (isPaused) {
      isPaused = false;
      player.resumePlaying();
    } else {
      isPaused = true;
      player.pausePlaying();
    }
  }
}

// 播放下一曲
void nextTrack() {
  currentTrack = (currentTrack + 1) % currentItems.size();
  // 跳过文件夹
  while (isDirectory[currentTrack]) {
    currentTrack = (currentTrack + 1) % currentItems.size();
  }
  playSelectedTrack();
}

// 播放上一曲
void prevTrack() {
  currentTrack = (currentTrack - 1 + currentItems.size()) % currentItems.size();
  // 跳过文件夹
  while (isDirectory[currentTrack]) {
    currentTrack = (currentTrack - 1 + currentItems.size()) % currentItems.size();
  }
  playSelectedTrack();
}

// 读取编码器状态
void updateEncoder() {
  static int lastCLK = digitalRead(ROTARY_CLK);
  static int lastDT = digitalRead(ROTARY_DT);
  
  int currentCLK = digitalRead(ROTARY_CLK);
  int currentDT = digitalRead(ROTARY_DT);
  
  if (currentCLK != lastCLK) {
    if (currentDT != currentCLK) {
      currentEncoderPos++;
    } else {
      currentEncoderPos--;
    }
    
    // 限制范围
    if (currentState == MenuState::FILE_BROWSER) {
      currentEncoderPos = constrain(currentEncoderPos, 0, currentItems.size() - 1);
    } else if (currentState == MenuState::VOLUME_ADJUST) {
      currentEncoderPos = constrain(currentEncoderPos, 0, 100);
      volume = currentEncoderPos;
      player.setVolume(volume);
    }
    
    encoderDirection = currentEncoderPos - lastEncoderPos;
    lastEncoderPos = currentEncoderPos;
  }
  
  lastCLK = currentCLK;
  lastDT = currentDT;
}

// 读取所有按钮状态（含去抖）
void updateButtonStates() {
  static unsigned long lastDebounceTime = 0;
  static const unsigned long debounceDelay = 50;
  
  static bool lastEncoderButton = digitalRead(ROTARY_SW);
  static bool lastPlayPauseButton = digitalRead(PLAY_PAUSE_SW);
  static bool lastNextButton = digitalRead(NEXT_SW);
  static bool lastPrevButton = digitalRead(PREV_SW);
  
  bool currentEncoderButton = digitalRead(ROTARY_SW);
  bool currentPlayPauseButton = digitalRead(PLAY_PAUSE_SW);
  bool currentNextButton = digitalRead(NEXT_SW);
  bool currentPrevButton = digitalRead(PREV_SW);
  
  if (currentEncoderButton != lastEncoderButton) {
    lastDebounceTime = millis();
  }
  
  if ((millis() - lastDebounceTime) > debounceDelay) {
    if (currentEncoderButton != lastEncoderButton) {
      encoderButtonPressed = !currentEncoderButton;  // 按下为LOW
    }
    
    if (currentPlayPauseButton != lastPlayPauseButton) {
      playPauseButtonPressed = !currentPlayPauseButton;
    }
    
    if (currentNextButton != lastNextButton) {
      nextButtonPressed = !currentNextButton;
    }
    
    if (currentPrevButton != lastPrevButton) {
      prevButtonPressed = !currentPrevButton;
    }
  }
  
  lastEncoderButton = currentEncoderButton;
  lastPlayPauseButton = currentPlayPauseButton;
  lastNextButton = currentNextButton;
  lastPrevButton = currentPrevButton;
}

// 处理用户输入
void handleUserInput() {
  updateEncoder();
  updateButtonStates();
  
  switch (currentState) {
    case MenuState::FILE_BROWSER:
      if (encoderDirection != 0) {
        drawFileBrowser();
        encoderDirection = 0;
      }
      
      if (encoderButtonPressed) {
        if (isDirectory[currentEncoderPos]) {
          // 进入文件夹
          if (currentItems[currentEncoderPos] == "..") {
            // 返回上级目录
            if (!pathHistory.empty()) {
              currentPath = pathHistory.back();
              pathHistory.pop_back();
            } else if (currentPath != "/") {
              // 如果没有历史记录，尝试手动返回上级
              int lastSlash = currentPath.lastIndexOf('/', currentPath.length() - 2);
              if (lastSlash >= 0) {
                currentPath = currentPath.substring(0, lastSlash + 1);
              } else {
                currentPath = "/";
              }
            }
          } else {
            // 记录当前路径到历史
            pathHistory.push_back(currentPath);
            // 进入子文件夹
            currentPath += currentItems[currentEncoderPos] + "/";
          }
          scanCurrentDirectory();
          currentEncoderPos = 0;
          drawFileBrowser();
        } else {
          // 选择文件，开始播放
          currentTrack = currentEncoderPos;
          currentState = MenuState::PLAYING;
          playSelectedTrack();
        }
        encoderButtonPressed = false;
      }
      break;
      
    case MenuState::PLAYING:
      if (encoderButtonPressed) {
        currentState = MenuState::VOLUME_ADJUST;
        currentEncoderPos = volume;
        drawVolumeScreen();
        encoderButtonPressed = false;
      }
      break;
      
    case MenuState::VOLUME_ADJUST:
      if (encoderDirection != 0) {
        drawVolumeScreen();
        encoderDirection = 0;
      }
      
      if (encoderButtonPressed) {
        currentState = MenuState::PLAYING;
        drawPlayingScreen();
        encoderButtonPressed = false;
      }
      break;
  }
}

void setup() {
  Serial.begin(115200);
  
  // 初始化GPIO引脚
  pinMode(ROTARY_CLK, INPUT_PULLUP);
  pinMode(ROTARY_DT, INPUT_PULLUP);
  pinMode(ROTARY_SW, INPUT_PULLUP);
  pinMode(PLAY_PAUSE_SW, INPUT_PULLUP);
  pinMode(NEXT_SW, INPUT_PULLUP);
  pinMode(PREV_SW, INPUT_PULLUP);
  
  // 初始化外设
  initDisplay();
  if (!initSDCard()) return;
  initPlayer();
  
  // 扫描根目录
  scanCurrentDirectory();
  
  // 初始界面
  currentState = MenuState::FILE_BROWSER;
  drawFileBrowser();
}

void loop() {
  handleUserInput();
  
  // 如果在播放状态但不在播放循环中（例如暂停时），更新显示
  if (currentState == MenuState::PLAYING && !player.isPlaying() && isPlaying) {
    drawPlayingScreen();
  }
  
  delay(10);  // 短暂延时减少CPU负载
}
