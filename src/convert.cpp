#include <algorithm>
#include <array>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <regex>
#include <sqlite3.h>
#include <sstream>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <sword/markupfiltmgr.h>
#include <sword/swmgr.h>
#include <sword/swmodule.h>
#include <sword/versekey.h>
#include <vector>

using std::cout;
using std::string;
using std::stringstream;

template <typename Out> void split(const string &s, char delim, Out result);

string get_verse(const char *_book, const char *_key);
std::vector<string> split(const string &s, char delim);
string exec(const char *cmd);
void sqlite_write(string _scripture, int _book, int _chapter, int _verse);
void sql_setup_db();
void sql_open();
void sql_close();

void do_conversion(string filename,
									 string (*func)(string, string, int, int, int));
string convert_from_sword(string key, string version, int bookNo, int chapter,
													int verse);
string convert_from_sqlite(string key, string version, int bookNo, int chapter,
													 int verse);
string get_sql_verse(int book, int chapter, int verse);
int max_chapter(int book);
int max_verse(int book, int chapter);

sqlite3 *ref_db;
sqlite3 *db;
char *zErrMsg = 0;
string version = "ESV2011";
string reference_db = "kjv.bbl.mybible";
string output_db = "out.bbl.mybible";

enum Testaments { OT = 0x02, NT = 0x04 };
inline Testaments operator|(Testaments x, Testaments y) {
	return static_cast<Testaments>(static_cast<int>(x) | static_cast<int>(y));
}

inline Testaments operator&(Testaments x, Testaments y) {
	return static_cast<Testaments>(static_cast<int>(x) & static_cast<int>(y));
}
Testaments which_testament = Testaments::OT | Testaments::NT;

static int callback(void *NotUsed, int argc, char **argv, char **azColName) {
	int i;
	for (i = 0; i < argc; i++) {
		cout << azColName[i] << (argv[i] ? argv[i] : "NULL") << "\n";
	}
	cout << ".";
	return 0;
}

static string trim(const string &_str) {
	size_t first = _str.find_first_not_of(' ');
	if (string::npos == first) {
		return _str;
	}
	size_t last = _str.find_last_not_of('\n');
	return _str.substr(first, (last - first + 1));
}

int main(int argc, char **argv) {
	string filename = "./verse.list";
	bool sword = false;
	for (uint8_t i = 0; i < argc; i++) {
		if (i + 1 < argc) {
			if (strcmp(argv[i], "-sv") == 0) {
				version = argv[i + 1];
				sword = true;
			}
			if (strcmp(argv[i], "-ref") == 0)
				reference_db = argv[i + 1];
			if (strcmp(argv[i], "-vl") == 0)
				filename = argv[i + 1];
			if (strcmp(argv[i], "-o") == 0)
				output_db = argv[i + 1];
		}
		if (strcmp(argv[i], "-nt") == 0)
			which_testament = Testaments::NT;
		if (strcmp(argv[i], "-ot") == 0)
			which_testament = Testaments::OT;
	}

	if (sword) {
		do_conversion(filename, &convert_from_sword);
	} else {
		do_conversion(filename, &convert_from_sqlite);
	}
	return 0;
}

void do_conversion(string filename,
									 string (*func)(string, string, int, int, int)) {
	sql_open();
	sql_setup_db();
	std::ifstream infile(filename);
	string line_;
	int book_ = 0;
	bool nt_only_ = (which_testament & Testaments::NT) == Testaments::NT &&
		!((which_testament & Testaments::OT) == Testaments::OT);
	bool ot_only_ = (which_testament & Testaments::OT) == Testaments::OT &&
		!((which_testament & Testaments::NT) == Testaments::NT);
	if (nt_only_)
		book_ = 39;
	while (std::getline(infile, line_)) {
		std::vector<string> v_ = split(line_, ' ');
		if (book_++ > 39 && ot_only_)
			break;
		int max_chap_ = max_chapter(book_);
		int i = 0;
		string book_name_ = v_[0];
		cout << book_name_;
		std::flush(cout);
		for (int i = 1; i <= max_chap_; i++) {
			if (i > 0) {
				int verse_count_ = max_verse(book_, i);
				for (int j = 1; j <= verse_count_; j++) {
					stringstream key_;
					key_ << book_name_ << " " << i << ":" << j;
					string scripture_ = func(key_.str(), version, book_, i, j);
					sqlite_write(scripture_, book_, i, j);
				}
			}
		}
	}
	sql_close();
}

string convert_from_sword(string key, string version, int bookNo, int chapter,
													int verse) {
	return get_verse(version.c_str(), key.c_str());
}

string convert_from_sqlite(string key, string version, int bookNo, int chapter,
													 int verse) {
	std::regex r("<((\\w)-?(\\w)?){4,}[^<]>");
	return std::regex_replace(get_sql_verse(bookNo, chapter, verse), r, "");
}

string convert_from_whatever(string key, string version, int bookNo,
														 int chapter, int verse) {
	stringstream ss;
	ss << chapter << ".";
	// ss << std::setfill('0') << std::setw(3) << verse;
}

void sql_setup_db() {
	string Bible_sql("CREATE TABLE IF NOT EXISTS \"Bible\" (\"Book\" "
									 "INT,\"Chapter\" INT,\"Verse\" INT,\"Scripture\" TEXT); "
									 "CREATE UNIQUE INDEX \"bible_key\" ON \"Bible\" (\"Book\" "
									 "ASC, \"Chapter\" ASC, \"Verse\" ASC);");
	string Details_sql("CREATE TABLE IF NOT EXISTS \"Details\" (\"Description\" "
										 "NVARCHAR(255),\"Abbreviation\" NVARCHAR(50),\"Comments\" "
										 "TEXT,\"Version\" TEXT, \"VersionDate\" DATETIME, "
										 "\"PublishDate\" DATETIME,\"RightToLeft\" BOOL,\"OT\" "
										 "BOOL,\"NT\" BOOL,\"Strong\" BOOL);");
	int rc_ = sqlite3_exec(db, Bible_sql.c_str(), callback, 0, &zErrMsg);
	rc_ = sqlite3_exec(db, Details_sql.c_str(), callback, 0, &zErrMsg);
}

void sql_open() {
	int rc_;
	rc_ = sqlite3_open(output_db.c_str(), &db);
	if (rc_) {
		std::cerr << "Can't open database: " << sqlite3_errmsg(db) << "\n";
	} else {
		std::cerr << "Opened database successfully.\n";
	}

	rc_ = sqlite3_open(reference_db.c_str(), &ref_db);
	if (rc_) {
		std::cerr << "Can't open reference database: " << sqlite3_errmsg(db)
							<< "\n";
	} else {
		std::cerr << "Opened reference db.\n";
	}
}

void sql_close() {
	sqlite3_close(db);
	sqlite3_close(ref_db);
	cout << "\nDBs closed\n";
}

void sqlite_write(string _scripture, int _book, int _chapter, int _verse) {
	const char *sql_ =
		"INSERT INTO Bible (Book, Chapter, Verse, Scripture) VALUES (?, ?, ?, ?)";
	sqlite3_stmt *stmt;
	const char *test;
	int rc = sqlite3_prepare(db, sql_, strlen(sql_), &stmt, &test);
	if (rc == SQLITE_OK) {
		sqlite3_bind_int(stmt, 1, _book);
		sqlite3_bind_int(stmt, 2, _chapter);
		sqlite3_bind_int(stmt, 3, _verse);
		sqlite3_bind_text(stmt, 4, _scripture.c_str(), strlen(_scripture.c_str()),
											0);

		sqlite3_step(stmt);
		sqlite3_finalize(stmt);
	}
}

string get_sql_verse(int book, int chapter, int verse) {
	sqlite3_stmt *stmt;
	const char *test;
	const char *sql = "SELECT Scripture FROM Bible WHERE Book = ? AND Chapter = "
		"? AND Verse = ?;";
	int rc = sqlite3_prepare(ref_db, sql, strlen(sql), &stmt, &test);
	if (rc == SQLITE_OK) {
		sqlite3_bind_int(stmt, 1, book);
		sqlite3_bind_int(stmt, 2, chapter);
		sqlite3_bind_int(stmt, 3, verse);
	}
	stringstream ss;
	for (;;) {
		rc = sqlite3_step(stmt);
		if (rc == SQLITE_DONE)
			break;
		if (rc != SQLITE_ROW) {
			cout << "I got nothing.\n";
			break;
		}
		ss << sqlite3_column_text(stmt, 0);
	}
	sqlite3_finalize(stmt);
	return ss.str();
}

int max_chapter(int book) {
	int res_ = 0;
	sqlite3_stmt *stmt;
	const char *sql = "SELECT Max(Chapter) FROM Bible WHERE Book = ?;";
	const char *test;
	int rc = sqlite3_prepare(ref_db, sql, strlen(sql), &stmt, &test);
	if (rc == SQLITE_OK) {
		sqlite3_bind_int(stmt, 1, book);
	}
	for (;;) {
		rc = sqlite3_step(stmt);
		if (rc == SQLITE_DONE) {
			break;
		}
		if (rc != SQLITE_ROW) {
			cout << "\nERR no row from query: book=" << book << "\n";
			break;
		}
		res_ = sqlite3_column_int(stmt, 0);
		cout << "\n * " << res_ << " chapters in ";
	}
	sqlite3_finalize(stmt);
	return res_;
}

int max_verse(int book, int chapter) {
	int res_ = 0;
	const char *sql_ =
		"SELECT Max(Verse) FROM Bible WHERE Book = ? AND Chapter = ?;";
	const char *test;
	sqlite3_stmt *stmt;
	int rc = sqlite3_prepare(ref_db, sql_, strlen(sql_), &stmt, &test);
	if (rc == SQLITE_OK) {
		sqlite3_bind_int(stmt, 1, book);
		sqlite3_bind_int(stmt, 2, chapter);
	}
	for (;;) {
		rc = sqlite3_step(stmt);
		if (rc == SQLITE_DONE)
			break;
		if (rc != SQLITE_ROW) {
			cout << "/nERR no row in " << sql_ << "\n";
			break;
		}
		res_ = sqlite3_column_int(stmt, 0);
		cout << "\nChapter " << chapter << ": " << res_ << " verses";
	}
	sqlite3_finalize(stmt);
	return res_;
}

template <typename Out> void split(const string &s, char delim, Out result) {
	stringstream ss(s);
	string item;
	while (std::getline(ss, item, delim)) {
		*(result++) = item;
	}
}

std::vector<string> split(const string &s, char delim) {
	std::vector<string> elems;
	split(s, delim, std::back_inserter(elems));
	return elems;
}

string exec(const char *cmd) {
	std::array<char, 128> buffer;
	string result;
	std::shared_ptr<FILE> pipe(popen(cmd, "r"), pclose);
	if (!pipe)
		throw std::runtime_error("popen() failed!");
	while (!feof(pipe.get())) {
		if (fgets(buffer.data(), 128, pipe.get()) != nullptr)
			result += buffer.data();
	}
	return result;
}

string get_verse(const char *_ver, const char *_key) {
	sword::SWMgr manager(
											 new sword::MarkupFilterMgr(sword::FMT_PLAIN, sword::ENC_UTF8));
	sword::SWModule *b = manager.getModule(_ver);
	if (!b)
		return "";
	sword::SWModule &book = *b;
	book.setProcessEntryAttributes(false);
	sword::VerseKey *vk = SWDYNAMIC_CAST(sword::VerseKey, book.getKey());
	if (vk) {
		vk->setText(_key);
		vk->setIntros(false);
	} else {
		book.setKey(_key);
	}
	sword::SWKey k(_key);
	stringstream ss_;
	book.renderText();
	ss_ << book.renderText();
	return ss_.str();
}
