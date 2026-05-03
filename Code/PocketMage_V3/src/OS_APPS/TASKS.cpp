// ooooooooooooo       .o.        .oooooo..o oooo    oooo  .oooooo..o   //
// 8'   888   `8      .888.      d8P'    `Y8 `888   .8P'  d8P'    `Y8   //
//      888          .8"888.     Y88bo.       888  d8'    Y88bo.        //
//      888         .8' `888.     `"Y8888o.   88888[       `"Y8888o.    //
//      888        .88ooo8888.         `"Y88b  888`88b.         `"Y88b  //
//      888       .8'     `888.  oo     .d8P  888  `88b.  oo     .d8P   //
//     o888o     o88o      o8888o 8""88888P'  o888o  o888o 8""88888P'   //  

#include <globals.h>
#include "esp32-hal-log.h"
#include "esp_log.h"
#if !OTA_APP // POCKETMAGE_OS
enum TasksState { TASKS0, TASKS0_NEWTASK, TASKS1, TASKS1_EDITTASK };
TasksState CurrentTasksState = TASKS0;

static constexpr const char* TAG = "TASKS";

static String currentWord = "";
static String currentLine = "";
uint8_t newTaskState = 0;
uint8_t editTaskState = 0;
String newTaskName = "";
String newTaskDueDate = "";
uint8_t selectedTask = 0;

void TASKS_INIT() {
  CurrentAppState = TASKS;
  CurrentTasksState = TASKS0;
  
  updateTaskArray();
  sortTasksByDueDate(tasks);
  
  #if POCKETMAGE_HW_VERSION != 2
    EINK().forceSlowFullUpdate(true);
  #endif
  newState = true;
}

void sortTasksByDueDate(std::vector<std::vector<String>> &tasks) {
  std::sort(tasks.begin(), tasks.end(), [](const std::vector<String> &a, const std::vector<String> &b) {
    return a[1] < b[1]; // Compare dueDate strings
  });
}

void updateTasksFile() {
  SDActive = true;
  pocketmage::setCpuSpeed(240);

  const char* tempFile = "/sys/tasks.tmp";
  const char* tasksFile = "/sys/tasks.txt";

  // Stream directly to the file to prevent Heap fragmentation / OOM crashes
  File file = global_fs->open(tempFile, FILE_WRITE);
  if (file) {
    for (size_t i = 0; i < tasks.size(); i++) {
      file.print(tasks[i][0]);
      file.print("|");
      file.print(tasks[i][1]);
      file.print("|");
      file.print(tasks[i][2]);
      file.print("|");
      file.println(tasks[i][3]); // println adds the newline
    }
    file.close();

    // Verify that the temp file was written correctly by checking its existence
    if (global_fs->exists(tempFile)) {
      PM_SDAUTO().deleteFile(*global_fs, tasksFile);
      PM_SDAUTO().renameFile(*global_fs, tempFile, tasksFile);
    }
  } else {
    ESP_LOGE(TAG, "Failed to write to temporary tasks file.");
    OLED().sysMessage("SAVE FAILED!",1000);
  }

  if (SAVE_POWER) pocketmage::setCpuSpeed(POWER_SAVE_FREQ);
  SDActive = false;
}

void addTask(String taskName, String dueDate, String priority, String completed) {
  tasks.push_back({taskName, dueDate, priority, completed});
  sortTasksByDueDate(tasks);
  updateTasksFile();
}

void updateTaskArray() {
  SDActive = true;
  pocketmage::setCpuSpeed(240);

  const char* tasksFile = "/sys/tasks.txt";

  // If the file doesn't exist, create it to ensure the app can run on first launch
  if (!global_fs->exists(tasksFile)) {
    File f = global_fs->open(tasksFile, FILE_WRITE);
    if (f) {
        f.close();
    } else {
        ESP_LOGE(TAG, "Failed to create tasks file.");
        if (SAVE_POWER) pocketmage::setCpuSpeed(POWER_SAVE_FREQ);
        SDActive = false;
        return;
    }
  }

  File file = global_fs->open(tasksFile, "r"); 
  if (!file) {
    ESP_LOGE(TAG, "Failed to open file to read: %s", tasksFile);
    if (SAVE_POWER) pocketmage::setCpuSpeed(POWER_SAVE_FREQ);
    SDActive = false;
    return;
  }

  tasks.clear(); // Clear the existing vector before loading the new data

  // Loop through the file, line by line
  while (file.available()) {
    String line = file.readStringUntil('\n');  
    line.trim();  
    
    // Skip empty lines
    if (line.length() == 0) {
      continue;
    }

    int delimiterPos1 = line.indexOf('|');
    int delimiterPos2 = line.indexOf('|', delimiterPos1 + 1);
    int delimiterPos3 = line.indexOf('|', delimiterPos2 + 1);

    // Basic validation for substrings
    if (delimiterPos1 == -1 || delimiterPos2 == -1 || delimiterPos3 == -1) {
        ESP_LOGW(TAG, "Malformed line in tasks file: %s", line.c_str());
        continue;
    }

    // Extract task name, due date, priority, and completed status
    String taskName  = line.substring(0, delimiterPos1);
    String dueDate   = line.substring(delimiterPos1 + 1, delimiterPos2);
    String priority  = line.substring(delimiterPos2 + 1, delimiterPos3);
    String completed = line.substring(delimiterPos3 + 1);

    // Add the task to the vector
    tasks.push_back({taskName, dueDate, priority, completed});
  }

  file.close(); 

  if (SAVE_POWER) pocketmage::setCpuSpeed(POWER_SAVE_FREQ);
  SDActive = false;
}

void deleteTask(int index) {
  if (index >= 0 && index < tasks.size()) {
    tasks.erase(tasks.begin() + index);
  }
}

String convertDateFormat(String yyyymmdd) {
  if (yyyymmdd.length() != 8) {
    ESP_LOGE(TAG, "Invalid Date: %s", yyyymmdd.c_str());
    return "Invalid";
  }

  String year = yyyymmdd.substring(2, 4);  // Get last two digits of the year
  String month = yyyymmdd.substring(4, 6);
  String day = yyyymmdd.substring(6, 8);

  return month + "/" + day + "/" + year;
}

void processKB_TASKS() {
  OLED().setPowerSave(false);
  int currentMillis = millis();
  disableTimeout = false;
  char inchar = 0;
  String input = "";

  switch (CurrentTasksState) {
    case TASKS0:
      KB().setKeyboardState(FUNC);
      
      // 1. Drain the hardware buffer continuously at loop speed
      inchar = KB().updateKeypress();

      // 2. Only process the actual input if the cooldown has expired
      if (currentMillis - KBBounceMillis >= KB_COOLDOWN) {  
        if (inchar != 0) {
          KBBounceMillis = currentMillis;

          //BKSP Recieved
          if (inchar == 127 || inchar == 8 || inchar == 12) {
            HOME_INIT();
            break;
          }
          // NEW TASK
          else if (inchar == '/' || inchar == 'n' || inchar == 'N') {
            CurrentTasksState = TASKS0_NEWTASK;
            KB().setKeyboardState(NORMAL);
            newTaskState = 0;
            break;
          }
          // SELECT A TASK
          else if (inchar >= '0' && inchar <= '9') {
            int taskIndex = (inchar == '0') ? 9 : (inchar - '1');  

            // SET SELECTED TASK
            if (taskIndex < tasks.size()) {
              selectedTask = taskIndex;
              // GO TO TASKS1
              CurrentTasksState = TASKS1;
              editTaskState = 0;
              newState = true;
            }
          }
        }
      }

      // 3. Update OLED at true OLED_MAX_FPS, completely independent of keyboard bounce
      currentMillis = millis();
      if (currentMillis - OLEDFPSMillis >= (1000/OLED_MAX_FPS)) {
        OLEDFPSMillis = currentMillis;
        if (CurrentTasksState == TASKS0) { // Make sure we didn't just jump to another menu
          OLED().oledWord(currentWord);
        }
      }
      break;

    case TASKS0_NEWTASK:
      if (newTaskState == 0) {
        // Step 1: Text entry for task name
        KB().setKeyboardState(NORMAL);
        input = textPrompt("Enter Task Name:");
        if (input == "_RETURN_") return;
        else if (input != "_EXIT_") {
          newTaskName = input;
          newTaskState = 1;
        } else {
          CurrentTasksState = TASKS0;
        }
      } 
      else if (newTaskState == 1) {
        // Step 2: Interactive Date Prompt
        String uiDate = datePrompt(""); // Returns "DD/MM/YYYY"

        // Convert "DD/MM/YYYY" to internal "YYYYMMDD" format for sorting
        if (uiDate.length() == 10) {
            newTaskDueDate = uiDate.substring(6, 10) + uiDate.substring(3, 5) + uiDate.substring(0, 2);

            // ADD NEW TASK
            addTask(newTaskName, newTaskDueDate, "0", "0");
            OLED().sysMessage("New Task Added",1000);
        }

        // RETURN TO MAIN MENU
        newTaskState = 0;
        CurrentTasksState = TASKS0;
        newState = true;
      }
      break;

    case TASKS1:
      disableTimeout = false;
      KB().setKeyboardState(FUNC);

      // 1. Drain the hardware buffer continuously at loop speed
      inchar = KB().updateKeypress();
      
      // 2. Only process the actual input if the cooldown has expired
      if (currentMillis - KBBounceMillis >= KB_COOLDOWN) {  
        if (inchar != 0) {
          KBBounceMillis = currentMillis;
        
          if (inchar == 127 || inchar == 8 || inchar == 12) {
            CurrentTasksState = TASKS0;
            #if POCKETMAGE_HW_VERSION != 2
              EINK().forceSlowFullUpdate(true);
            #endif
            newState = true;
            break;
          }
          else if (inchar >= '1' && inchar <= '4') {
              
            if (selectedTask >= 0 && selectedTask < tasks.size()) {
                if (inchar == '1') {      // RENAME TASK
                  KB().setKeyboardState(NORMAL);
                  input = textPrompt("Enter a new task name:");
                  if (input == "_RETURN_") return;
                  else if (input != "_EXIT_") {
                    OLED().oledWord("updating task...");
                    tasks[selectedTask][0] = input;
                    updateTasksFile();

                    CurrentTasksState = TASKS0;
                    #if POCKETMAGE_HW_VERSION != 2
                      EINK().forceSlowFullUpdate(true);
                    #endif
                    newState = true;
                  }
                }
                else if (inchar == '2') { // CHANGE DUE DATE
                  // Call the new interactive UI
                  String uiDate = datePrompt(tasks[selectedTask][1]);
                  
                  if (uiDate != "_EXIT_" && uiDate.length() == 10) {
                    OLED().oledWord("updating task...");

                    // Convert "DD/MM/YYYY" to internal "YYYYMMDD"
                    newTaskDueDate = uiDate.substring(6, 10) + uiDate.substring(3, 5) + uiDate.substring(0, 2);

                    // UPDATE DUE DATE
                    tasks[selectedTask][1] = newTaskDueDate;
                    updateTasksFile();

                    // RETURN
                    CurrentTasksState = TASKS0;
                    #if POCKETMAGE_HW_VERSION != 2
                      EINK().forceSlowFullUpdate(true);
                    #endif
                    newState = true;
                  }
                }
                else if (inchar == '3') { // DELETE TASK
                  int response = boolPrompt("Delete Task?");
                  if (response == 1) {
                    OLED().oledWord("deleting...");
                    deleteTask(selectedTask);
                    updateTasksFile();

                    CurrentTasksState = TASKS0;
                    #if POCKETMAGE_HW_VERSION != 2
                      EINK().forceSlowFullUpdate(true);
                    #endif
                    newState = true;
                  }
                }
                else if (inchar == '4') { // COPY TASK
                  OLED().oledWord("copying...");
                  addTask(tasks[selectedTask][0]+"_COPY", tasks[selectedTask][1], "0", "0");

                  CurrentTasksState = TASKS0;
                  #if POCKETMAGE_HW_VERSION != 2
                    EINK().forceSlowFullUpdate(true);
                  #endif
                  newState = true;
                }
            }
          }
        }
      }

      // 3. Update OLED at true OLED_MAX_FPS, completely independent of keyboard bounce
      currentMillis = millis();
      if (currentMillis - OLEDFPSMillis >= (1000/OLED_MAX_FPS)) {
        OLEDFPSMillis = currentMillis;
        if (CurrentTasksState == TASKS1) { // Make sure we didn't just jump to another menu
          OLED().oledWord(currentWord);
        }
      }
      break;
  }
}

void einkHandler_TASKS() {
  switch (CurrentTasksState) {
    case TASKS0:
      if (newState) {
        newState = false;
        EINK().resetDisplay();

        // DRAW APP
        display.drawBitmap(0, 0, tasksApp0, 320, 218, GxEPD_BLACK);

        if (!tasks.empty()) {
          ESP_LOGV(TAG, "Printing Tasks");
          EINK().drawStatusBar("Select (1-0),New Task (N)");

          int loopCount = std::min((int)tasks.size(), MAX_FILES);
          for (int i = 0; i < loopCount; i++) {
            display.setFont(&FreeSerif9pt7b);
            // PRINT TASK NAME
            display.setCursor(29, 54 + (17 * i));
            display.print(tasks[i][0].c_str());
            // PRINT TASK DUE DATE
            display.setCursor(231, 54 + (17 * i));
            display.print(convertDateFormat(tasks[i][1]).c_str());

            ESP_LOGI("TASKS", "%s, %s", tasks[i][0].c_str(), convertDateFormat(tasks[i][1]).c_str()); 
          }
        }
        else EINK().drawStatusBar("No Tasks! Add New Task (N)");

        EINK().refresh();
      }
      break;
      
    case TASKS0_NEWTASK:
      if (newState) {
        newState = false;
        EINK().resetDisplay();

        // DRAW APP
        display.drawBitmap(0, 0, tasksApp0, 320, 218, GxEPD_BLACK);

        if (!tasks.empty()) {
          ESP_LOGV(TAG, "Printing Tasks");

          int loopCount = std::min((int)tasks.size(), MAX_FILES);
          for (int i = 0; i < loopCount; i++) {
            display.setFont(&FreeSerif9pt7b);
            // PRINT TASK NAME
            display.setCursor(29, 54 + (17 * i));
            display.print(tasks[i][0].c_str());
            // PRINT TASK DUE DATE
            display.setCursor(231, 54 + (17 * i));
            display.print(convertDateFormat(tasks[i][1]).c_str());

            ESP_LOGI("TASKS", "%s, %s", tasks[i][0].c_str(), convertDateFormat(tasks[i][1]).c_str()); 
          }
        }
        
        switch (newTaskState) {
          case 0:
            EINK().drawStatusBar("Enter Task Name:");
            break;
          case 1:
            EINK().drawStatusBar("Set Due Date on OLED");
            break;
        }

        EINK().refresh();
      }
      break;
      
    case TASKS1:
      if (newState) {
        newState = false;
        EINK().resetDisplay();

        // DRAW APP
        EINK().drawStatusBar("T:" + tasks[selectedTask][0]);
        display.drawBitmap(0, 0, tasksApp1, 320, 218, GxEPD_BLACK);

        EINK().refresh();
      }
      break;
  }
}
#endif