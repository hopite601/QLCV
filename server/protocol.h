#ifndef PROTOCOL_H
#define PROTOCOL_H

#define CMD_REGISTER        "REGISTER"
#define CMD_LOGIN           "LOGIN"

#define CMD_CREATE_PROJECT  "CREATE_PROJECT"
#define CMD_INVITE_MEMBER   "INVITE_MEMBER"

#define CMD_CREATE_TASK     "CREATE_TASK"
#define CMD_ASSIGN_TASK     "ASSIGN_TASK"

#define CMD_LIST_PROJECT    "LIST_PROJECT"
#define CMD_LIST_TASK       "LIST_TASK"

// Extended features
#define CMD_UPDATE_TASK_STATUS   "UPDATE_TASK_STATUS"   // UPDATE_TASK_STATUS|task_id|NOT_STARTED|IN_PROGRESS|DONE
#define CMD_UPDATE_TASK_PROGRESS "UPDATE_TASK_PROGRESS" // UPDATE_TASK_PROGRESS|task_id|0..100
#define CMD_SET_TASK_DATES       "SET_TASK_DATES"       // SET_TASK_DATES|task_id|YYYY-MM-DD|YYYY-MM-DD
#define CMD_LIST_TASK_DETAIL     "LIST_TASK_DETAIL"     // LIST_TASK_DETAIL|task_id

#define CMD_ADD_COMMENT          "ADD_COMMENT"          // ADD_COMMENT|task_id|content
#define CMD_LIST_COMMENTS        "LIST_COMMENTS"        // LIST_COMMENTS|task_id

#define CMD_ADD_ATTACHMENT       "ADD_ATTACHMENT"       // ADD_ATTACHMENT|task_id|filename|filepath
#define CMD_LIST_ATTACHMENTS     "LIST_ATTACHMENTS"     // LIST_ATTACHMENTS|task_id

#define CMD_SEND_CHAT            "SEND_CHAT"            // SEND_CHAT|project_id|content
#define CMD_LIST_CHAT            "LIST_CHAT"            // LIST_CHAT|project_id|after_id

#define CMD_LIST_TASK_GANTT      "LIST_TASK_GANTT"      // LIST_TASK_GANTT|project_id

// ===================== REPORTS =====================
// Report is tied to a project.
// ADD_REPORT|project_id|title|description
#define CMD_ADD_REPORT              "ADD_REPORT"

// LIST_REPORTS|project_id
#define CMD_LIST_REPORTS            "LIST_REPORTS"

// GET_REPORT|report_id
#define CMD_GET_REPORT              "GET_REPORT"

// UPDATE_REPORT|report_id|title|description
#define CMD_UPDATE_REPORT           "UPDATE_REPORT"

// DELETE_REPORT|report_id
#define CMD_DELETE_REPORT           "DELETE_REPORT"

// ===================== REPORT COMMENTS =====================
// ADD_REPORT_COMMENT|report_id|content
#define CMD_ADD_REPORT_COMMENT      "ADD_REPORT_COMMENT"

// LIST_REPORT_COMMENTS|report_id
#define CMD_LIST_REPORT_COMMENTS    "LIST_REPORT_COMMENTS"

// DELETE_REPORT_COMMENT|comment_id
#define CMD_DELETE_REPORT_COMMENT   "DELETE_REPORT_COMMENT"

// ===================== REPORT FILES =====================
// ADD_REPORT_FILE|report_id|filename|filepath
#define CMD_ADD_REPORT_FILE         "ADD_REPORT_FILE"

// LIST_REPORT_FILES|report_id
#define CMD_LIST_REPORT_FILES       "LIST_REPORT_FILES"

// DELETE_REPORT_FILE|file_id
#define CMD_DELETE_REPORT_FILE      "DELETE_REPORT_FILE"

#endif
