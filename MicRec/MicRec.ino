#include <SDHCI.h>
#include <MediaRecorder.h>
#include <MemoryUtil.h>

//定数パラメタ定義
const String FILEPREFIX = "sound_"; //録音ファイル名の接頭文字
const int    RECFILECNT = 40;       //録音ファイル数
const float  RECTIME = 0.5f;        //録音時間
const int    REC_THRESHOLD = 250;   //録音を開始する振幅の閾値

//LED接続ピン設定
#define PIN1 7    //青LED接続ピン
#define PIN2 6    //黄LED接続ピン

//録音機能を実現するクラス
MediaRecorder *theRecorder;

//SDカードアクセス用のクラス
SDClass theSD;

//録音データ一時格納用バッファ
const int32_t buffer_size = 4096;
uint8_t       s_buffer[buffer_size];

//WAVデータ格納用バッファ(48KHz・モノラルで RECTIME秒録音するために必要なサンプル数)
const int recSize = (int)(48000 * RECTIME);
short wavData[recSize];             //16bitで録音するので各サンプルはshort型
int recIdx = 0;

//録音回数カウント用
int fileCnt = 0;

//MediaRecorderのイベント(録音開始とか終了とか)処理用コールバック関数
static bool mediarecorder_done_callback(AsRecorderEvent event, uint32_t result, uint32_t sub_result)
{
  //今回は特にやりたい事がないので、引数の値を適当に出力するだけ
  printf("## MediaRecorder Event : %x %x\n", event, result);
  return true;
}

//初期化処理
void setup()
{
  //LEDを初期化(消灯)
  pinMode(PIN1, OUTPUT);
  pinMode(PIN2, OUTPUT);
  digitalWrite(PIN1, LOW);
  digitalWrite(PIN2, LOW);
    
  //録音機能に必要なメモリを確保する
  initMemoryPools();
  createStaticPools(MEM_LAYOUT_RECORDER);

  //録音機能(MediaRecorder)を初期化
  theRecorder = MediaRecorder::getInstance();
  theRecorder->begin();
  
  //録音用のクロックを標準に設定
  theRecorder->setCapturingClkMode(MEDIARECORDER_CAPCLK_NORMAL);

  //アナログマイクからの入力に設定・イベントコールバック関数を設定
  theRecorder->activate(AS_SETRECDR_STS_INPUTDEVICE_MIC, mediarecorder_done_callback);
  theRecorder->setMicGain(210);   //マイクの音量調整(0～210)

  //マイクの準備完了を少し待つ
  usleep(100000);

  //録音設定(WAV形式・モノラル・48KHz・16bit)
  theRecorder->init(AS_CODECTYPE_WAV, AS_CHANNEL_MONO, AS_SAMPLINGRATE_48000, AS_BITLENGTH_16, 0);

  //録音開始！
  theRecorder->start();
  puts("Recording Start!");
}

//メインループ
void loop()
{
  uint32_t size = 0;

  //録音データをs_bufferに取り出す
  err_t err = theRecorder->readFrames(s_buffer, buffer_size, &size);

  //エラーだった場合(OK・バッファ不足以外の場合)は終了処理(done)に飛ばす
  if(err != MEDIARECORDER_ECODE_OK && err != MEDIARECORDER_ECODE_INSUFFICIENT_BUFFER_AREA)
  {
    puts("Recording Error!");
    done();
  }
  //録音データが取得できた場合、録音データ処理(signal_process)に飛ばす
  else if(size > 0)
  {
    signal_process(size);
    //RECFILECNT分の録音が完了していたら終了処理(done)に飛ばす
    if(fileCnt > RECFILECNT)
      done();
  }
}

//録音データ処理
void signal_process(uint32_t size)
{
  //録音データを16bit符号付き整数として扱えるようにキャスト
  short *p = (short*)s_buffer;

  //１サンプルごとのループ(１サンプル16bitなのでループ回数はsize/2)
  for(int i = 0; i < size/2; i++)
  {
    //録音開始前(WAVデータとして取り出し始める前)かつ振幅がREC_THRESHOLDを超えたら
    if(recIdx == 0 && abs(p[i]) > REC_THRESHOLD)
    {
      //録音開始(WAVデータとして取り出し始める)
      puts("Record to File Start");
      wavData[recIdx] = p[i];
      recIdx++;
      //青色LED点灯
      digitalWrite(PIN1, HIGH);
    }
    //録音中かつ録音時間がRECTIME未満の場合
    else if(recIdx != 0 && recIdx < recSize)
    {
      //サンプル値を取り出してwavDataに格納
      wavData[recIdx] = p[i];
      recIdx++;
    }
    //録音時間がRECTIMEになった(＝RECTIME秒ぶんWAVデータの取り出しが完了した)場合
    else if(recIdx == recSize)
    {
      puts("Record to File End");
      //録音停止
      theRecorder->stop();
      
      //青色LED消灯・黄色LED点灯
      digitalWrite(PIN1, LOW);
      digitalWrite(PIN2, HIGH);
      
      //拡張子を除くファイル名を作成しファイル書き込み処理(writeFile)を呼び出す
      String file = FILEPREFIX + fileCnt;
      writeFile(file.c_str(), wavData, recSize);
      
      //次の録音開始に備える
      fileCnt++;
      recIdx = 0;

      //黄色LED消灯
      digitalWrite(PIN2, LOW);

      //録音再開
      theRecorder->start();
    }
  }
}

//ファイル書き込み処理(wav, csv)
void writeFile(const char* file, short* buffer, int size){
  File f;
  char str[10];

  //書き込むためのWAVファイルを開く
  String wavFileName = String(file)+".wav";
  theSD.remove(wavFileName);     //同名のファイルがあったら先に削除しておく
  f = theSD.open(wavFileName, FILE_WRITE);  
  puts("Saving to WAV File");

  //WAVファイルのヘッダを出力
  theRecorder->writeWavHeader(f);
  //WAVデータ本体を書き込んでファイルを閉じる
  f.write((uint8_t*)buffer, size*2);
  f.close();

  //書き込むためのCSVファイルを開く
  String csvFileName = String(file)+".csv";
  theSD.remove(csvFileName);     //同名のファイルがあったら先に削除しておく
  f = theSD.open(csvFileName, FILE_WRITE);  
  puts("Saving to CSV File");

  //データの１／６(＝8KHzのデータとして)をfloat型の数値にして書き込む
  for(int i = 0; i < size; i+=6){
    //値を実数文字列化する(32768ではなく4096で割っているのは元の音量が小さいため)
    sprintf(str, "%0.4f", (float)buffer[i]/4096.0);
    f.println(str);
  }
  f.close();
}

//全録音終了処理
void done()
{
  //録音停止と録音オブジェクトの後片付け
  theRecorder->stop();
  theRecorder->deactivate();
  theRecorder->end();

  puts("End Recording");  
  exit(1);
}
