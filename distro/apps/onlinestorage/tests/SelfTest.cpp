/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 *
 * Simulated providers only. No account, no network, no real token.
 */

#include "SelfTest.h"

#include "Dropbox.h"
#include "JsonUtil.h"
#include "LocalFile.h"
#include "MemoryCloud.h"
#include "OAuth.h"
#include "OneDrive.h"
#include "Redirect.h"
#include "Retry.h"
#include "ScriptedTransport.h"
#include "Sha256.h"
#include "SyncEngine.h"
#include "Url.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>


static int gFailed = 0;


static void
expect(bool condition, const char* file, int line, const char* text)
{
	if (condition)
		return;
	fprintf(stderr, "FAIL %s:%d %s\n", file, line, text);
	gFailed++;
}


#define CHECK(condition) expect((condition), __FILE__, __LINE__, #condition)


static void
write_text(const char* root, const char* relative, const char* text)
{
	BString path;
	path << root << "/" << relative;
	LocalWriteAtomic(path.String(), text, strlen(text));
}


static BString
read_text(const char* root, const char* relative)
{
	BString path;
	path << root << "/" << relative;
	FILE* file = fopen(path.String(), "rb");
	if (file == NULL)
		return BString("");
	BString out;
	char buffer[1024];
	size_t n = fread(buffer, 1, sizeof(buffer) - 1, file);
	fclose(file);
	buffer[n] = '\0';
	out.SetTo(buffer);
	return out;
}


static void
test_sha_and_pkce()
{
	BString hex;
	Sha256Bytes("abc", 3, hex);
	CHECK(hex == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
	BString verifier = "dBjftJeZ4CVP-mB92K27uhbUJU1p1r_wW1gFWFOEjXk";
	CHECK(PkceChallengeS256(verifier)
		== "E9Melhoa2OwvFrEMTJguCHaoeK1t8URWbuGJSstw-cM");
}


static void
test_oauth_request()
{
	MemoryTokenStore store;
	OAuthConfig config = OneDriveOAuthConfig();
	OAuthClient oauth(config, store);
	BString url;
	CHECK(oauth.Begin(url) == B_OK);
	CHECK(url.FindFirst("code_challenge_method=S256") >= 0);
	CHECK(url.FindFirst("code_challenge=") >= 0);
	CHECK(url.FindFirst("client_secret") < 0);
	CHECK(url.FindFirst("127.0.0.1") >= 0);
	CHECK(url.FindFirst(oauth.State().String()) >= 0);
	BString code;
	CHECK(oauth.TakeCode("code=abc&state=nope", code) == B_NOT_ALLOWED);
	BString query;
	query << "code=from-provider&state=" << oauth.State();
	CHECK(oauth.TakeCode(query, code) == B_OK);
	CHECK(code == "from-provider");

	OAuthConfig dropbox = DropboxOAuthConfig();
	OAuthClient dropboxOAuth(dropbox, store);
	CHECK(dropboxOAuth.Begin(url) == B_OK);
	CHECK(url.FindFirst("token_access_type=offline") >= 0);
	/* "token_access_type" contains the letters access_type. */
	CHECK(url.FindFirst("&access_type=") < 0);
	CHECK(url.FindFirst("?access_type=") < 0);
	CHECK(url.FindFirst("client_secret") < 0);
}


static void
test_loopback()
{
	int fd = -1;
	uint16 port = 0;
	CHECK(BindLoopback(fd, 0, port) == B_OK);
	CHECK(port != 0);
	int client = socket(AF_INET, SOCK_STREAM, 0);
	struct sockaddr_in address;
	memset(&address, 0, sizeof(address));
	address.sin_family = AF_INET;
	address.sin_port = htons(port);
	inet_pton(AF_INET, "127.0.0.1", &address.sin_addr);
	CHECK(connect(client, (struct sockaddr*)&address, sizeof(address)) == 0);
	const char* bad = "GET /?code=abc&state=wrong HTTP/1.1\r\nHost: 127.0.0.1\r\n\r\n";
	send(client, bad, strlen(bad), 0);
	int accepted = accept(fd, NULL, NULL);
	BString code;
	CHECK(ReadLoopbackCode(accepted, "right-state", code) == B_NOT_ALLOWED);
	close(accepted);
	close(client);

	client = socket(AF_INET, SOCK_STREAM, 0);
	CHECK(connect(client, (struct sockaddr*)&address, sizeof(address)) == 0);
	const char* good = "GET /?code=real-code&state=right-state HTTP/1.1\r\nHost: 127.0.0.1\r\n\r\n";
	send(client, good, strlen(good), 0);
	accepted = accept(fd, NULL, NULL);
	CHECK(ReadLoopbackCode(accepted, "right-state", code) == B_OK);
	CHECK(code == "real-code");
	close(accepted);
	close(client);
	close(fd);
}


static void
test_redirect_and_retry()
{
	RedirectStep step;
	std::vector<BString> hops;
	CHECK(PlanRedirect("https://graph.microsoft.com/v1.0/a",
		"https://graph.microsoft.com/v1.0/a", hops,
		"https://download.example/file", step) == B_OK);
	CHECK(step.stripAuthorization);
	CHECK(PlanRedirect("https://graph.microsoft.com/v1.0/a",
		"https://graph.microsoft.com/v1.0/a", hops,
		"https://graph.microsoft.com/v1.0/b", step) == B_OK);
	CHECK(!step.stripAuthorization);
	CHECK(PlanRedirect("https://graph.microsoft.com/v1.0/a",
		"https://graph.microsoft.com/v1.0/a", hops,
		"http://graph.microsoft.com/v1.0/a", step) == B_NOT_ALLOWED);
	CHECK(PlanRedirect("https://graph.microsoft.com/v1.0/a",
		"https://graph.microsoft.com/v1.0/a", hops,
		"HTTP://attacker.example/collect", step) == B_NOT_ALLOWED);

	ScriptedTransport transport;
	transport.AddRetry("GET", "/children", 429, "2", "{\"error\":\"throttle\"}");
	transport.Add("GET", "/children", 200,
		"{\"value\":[{\"id\":\"1\",\"name\":\"a.txt\",\"size\":1,\"cTag\":\"c\","
		"\"file\":{},\"parentReference\":{\"path\":\"/drive/root:/Cloud\"}}]}");
	NoSleep sleep;
	CaptureLog log;
	HttpExchange exchange;
	exchange.method = "GET";
	exchange.url = "https://graph.microsoft.com/v1.0/children";
	CHECK(CallWithRetry(transport, exchange, sleep, log, "onedrive") == B_OK);
	CHECK(exchange.status == 200);
	CHECK(sleep.waited == 2);
	CHECK(transport.Unused() == 0);
}


static void
test_recorded_providers()
{
	MemoryTokenStore store;
	BMessage token;
	TokenSet(token, "first-access", "first-refresh", 4LL * 1000 * 1000 * 1000 * 1000 * 1000);
	store.Save("onedrive", token);
	store.Save("dropbox", token);
	NoSleep sleep;
	CaptureLog log;

	ScriptedTransport onedriveTransport;
	onedriveTransport.Add("GET", "/children", 401,
		"{\"error\":{\"code\":\"InvalidAuthenticationToken\"}}");
	onedriveTransport.Add("POST", "/oauth2/v2.0/token", 200,
		"{\"access_token\":\"second-access\",\"refresh_token\":\"second-refresh\","
		"\"expires_in\":3600}");
	onedriveTransport.Add("GET", "/children", 200,
		"{\"value\":[{\"id\":\"1\",\"name\":\"a.txt\",\"size\":4,\"cTag\":\"ctag\","
		"\"file\":{},\"parentReference\":{\"path\":\"/drive/root:/Cloud\"}}]}");
	OAuthClient onedriveOAuth(OneDriveOAuthConfig(), store);
	OneDriveProvider onedrive(onedriveTransport, onedriveOAuth, sleep, log, "Cloud");
	std::vector<CloudItem> items;
	CHECK(onedrive.List("", items) == B_OK);
	CHECK(items.size() == 1);
	CHECK(items[0].path == "a.txt");
	CHECK(items[0].revision == "ctag");
	CHECK(log.text.FindFirst("second-access") < 0);
	CHECK(log.text.FindFirst("first-refresh") < 0);
	CHECK(log.text.FindFirst("second-refresh") < 0);

	ScriptedTransport deltaTransport;
	deltaTransport.Add("GET", "/delta", 200,
		"{\"value\":[{\"id\":\"9\",\"name\":\"gone.txt\",\"deleted\":{\"state\":\"deleted\"},"
		"\"parentReference\":{\"path\":\"/drive/root:/Cloud\"}}],"
		"\"@odata.deltaLink\":\"https://graph.microsoft.com/v1.0/me/drive/root:/Cloud:/delta?token=3\"}");
	OAuthClient deltaOAuth(OneDriveOAuthConfig(), store);
	OneDriveProvider delta(deltaTransport, deltaOAuth, sleep, log, "Cloud");
	BString cursor;
	items.clear();
	CHECK(delta.Changes("", cursor, items) == B_OK);
	CHECK(items.size() == 1);
	CHECK(items[0].deleted);
	CHECK(items[0].path == "gone.txt");
	CHECK(cursor.FindFirst("token=3") >= 0);

	ScriptedTransport uploadTransport;
	uploadTransport.Add("POST", "createUploadSession", 200,
		"{\"uploadUrl\":\"https://upload.memory.test/s1\"}");
	uploadTransport.Add("PUT", "upload.memory.test/s1", 416, "{\"error\":\"range\"}");
	uploadTransport.Add("GET", "upload.memory.test/s1", 200,
		"{\"nextExpectedRanges\":[\"0-\"]}");
	uploadTransport.Add("PUT", "upload.memory.test/s1", 201,
		"{\"id\":\"1\",\"name\":\"a.txt\",\"size\":1,\"cTag\":\"after\","
		"\"file\":{},\"parentReference\":{\"path\":\"/drive/root:/Cloud\"}}");
	char dir[] = "/tmp/r5os-upXXXXXX";
	CHECK(mkdtemp(dir) != NULL);
	write_text(dir, "a.txt", "Z");
	OAuthClient uploadOAuth(OneDriveOAuthConfig(), store);
	OneDriveProvider upload(uploadTransport, uploadOAuth, sleep, log, "Cloud");
	BString file;
	file << dir << "/a.txt";
	CHECK(upload.Upload("a.txt", file.String()) == B_OK);
	CHECK(uploadTransport.Unused() == 0);

	ScriptedTransport dropboxTransport;
	dropboxTransport.Add("POST", "/files/list_folder", 200,
		"{\"entries\":[{\".tag\":\"file\",\"name\":\"a.txt\",\"path_display\":\"/Cloud/a.txt\","
		"\"id\":\"id:1\",\"size\":2,\"rev\":\"r1\",\"content_hash\":\"h1\"},"
		"{\".tag\":\"deleted\",\"name\":\"old.txt\",\"path_display\":\"/Cloud/old.txt\","
		"\"id\":\"id:2\"}],\"cursor\":\"cur\",\"has_more\":false}");
	OAuthClient dropboxOAuth(DropboxOAuthConfig(), store);
	DropboxProvider dropbox(dropboxTransport, dropboxOAuth, sleep, log, "Cloud");
	items.clear();
	CHECK(dropbox.Changes("", cursor, items) == B_OK);
	CHECK(items.size() == 2);
	CHECK(items[0].path == "a.txt");
	CHECK(!items[0].deleted);
	CHECK(items[1].deleted);
	CHECK(items[1].path == "old.txt");
}


static int
sync_case(const char* provider)
{
	char local[] = "/tmp/r5os-syncXXXXXX";
	if (mkdtemp(local) == NULL)
		return 1;
	BString state;
	state << local << ".state";
	MemoryCloud cloud(provider, "Cloud");
	MemoryTokenStore store;
	BMessage token;
	TokenSet(token, "access-token", "refresh-token",
		4LL * 1000 * 1000 * 1000 * 1000 * 1000);
	store.Save(provider, token);
	NoSleep sleep;
	CaptureLog log;
	OAuthConfig config = strcmp(provider, "onedrive") == 0
		? OneDriveOAuthConfig() : DropboxOAuthConfig();
	config.account = provider;
	OAuthClient oauth(config, store);
	SyncResult result;
	int before = gFailed;

	if (strcmp(provider, "onedrive") == 0) {
		OneDriveProvider remote(cloud, oauth, sleep, log, "Cloud");
		SyncEngine engine(remote);
		write_text(local, "new.txt", "created");
		CHECK(engine.Sync(local, state.String(), result) == B_OK);
		CHECK(cloud.Read("new.txt") == "created");

		cloud.PutFile("from-cloud.txt", "remote-new");
		CHECK(engine.Sync(local, state.String(), result) == B_OK);
		CHECK(read_text(local, "from-cloud.txt") == "remote-new");

		write_text(local, "new.txt", "local-changed");
		CHECK(engine.Sync(local, state.String(), result) == B_OK);
		CHECK(cloud.Read("new.txt") == "local-changed");

		cloud.PutFile("from-cloud.txt", "remote-changed");
		CHECK(engine.Sync(local, state.String(), result) == B_OK);
		CHECK(read_text(local, "from-cloud.txt") == "remote-changed");

		unlink((BString(local) << "/new.txt").String());
		CHECK(engine.Sync(local, state.String(), result) == B_OK);
		CHECK(!cloud.Has("new.txt"));
		CHECK(engine.Sync(local, state.String(), result) == B_OK);
		CHECK(!cloud.Has("new.txt"));
		CHECK(!LocalExists((BString(local) << "/new.txt").String()));

		cloud.Remove("from-cloud.txt");
		CHECK(engine.Sync(local, state.String(), result) == B_OK);
		CHECK(!LocalExists((BString(local) << "/from-cloud.txt").String()));
		CHECK(engine.Sync(local, state.String(), result) == B_OK);
		CHECK(!cloud.Has("from-cloud.txt"));

		write_text(local, "move-me.txt", "same-bytes");
		CHECK(engine.Sync(local, state.String(), result) == B_OK);
		CHECK(LocalRename((BString(local) << "/move-me.txt").String(),
			(BString(local) << "/moved.txt").String()) == B_OK);
		CHECK(engine.Sync(local, state.String(), result) == B_OK);
		CHECK(!cloud.Has("move-me.txt"));
		CHECK(cloud.Read("moved.txt") == "same-bytes");

		cloud.Rename("moved.txt", "renamed.txt");
		CHECK(engine.Sync(local, state.String(), result) == B_OK);
		CHECK(read_text(local, "renamed.txt") == "same-bytes");
		CHECK(!LocalExists((BString(local) << "/moved.txt").String()));

		write_text(local, "renamed.txt", "local-side");
		cloud.PutFile("renamed.txt", "remote-side");
		CHECK(engine.Sync(local, state.String(), result) == B_OK);
		CHECK(read_text(local, "renamed.txt") == "remote-side");
		CHECK(read_text(local, "renamed (conflict).txt") == "local-side");
		CHECK(cloud.Read("renamed.txt") == "remote-side");
		CHECK(cloud.Read("renamed (conflict).txt") == "local-side");
		CHECK(result.conflicts >= 1);

		BString nested;
		nested << local << "/docs/a.txt";
		CHECK(LocalMakeParents(nested.String()) == B_OK);
		write_text(local, "docs/a.txt", "inside");
		CHECK(engine.Sync(local, state.String(), result) == B_OK);
		CHECK(cloud.Read("docs/a.txt") == "inside");
		cloud.Remove("docs");
		CHECK(engine.Sync(local, state.String(), result) == B_OK);
		CHECK(!LocalExists((BString(local) << "/docs/a.txt").String()));
		CHECK(!cloud.Has("docs"));
		CHECK(engine.Sync(local, state.String(), result) == B_OK);
		CHECK(!cloud.Has("docs"));
		CHECK(!cloud.Has("docs/a.txt"));

		BString tree;
		tree << local << "/tree/file.txt";
		CHECK(LocalMakeParents(tree.String()) == B_OK);
		write_text(local, "tree/file.txt", "gone");
		CHECK(engine.Sync(local, state.String(), result) == B_OK);
		CHECK(cloud.Has("tree/file.txt"));
		unlink(tree.String());
		rmdir((BString(local) << "/tree").String());
		CHECK(engine.Sync(local, state.String(), result) == B_OK);
		CHECK(!cloud.Has("tree"));
		CHECK(!cloud.Has("tree/file.txt"));
		CHECK(!LocalExists((BString(local) << "/tree").String()));
		CHECK(engine.Sync(local, state.String(), result) == B_OK);
		CHECK(!cloud.Has("tree/file.txt"));

		BString deep;
		deep << local << "/docs/sub/a.txt";
		CHECK(LocalMakeParents(deep.String()) == B_OK);
		write_text(local, "docs/sub/a.txt", "deep");
		CHECK(engine.Sync(local, state.String(), result) == B_OK);
		CHECK(cloud.Read("docs/sub/a.txt") == "deep");
		cloud.Remove("docs");
		CHECK(engine.Sync(local, state.String(), result) == B_OK);
		CHECK(!LocalExists((BString(local) << "/docs").String()));
		CHECK(!cloud.Has("docs"));
		CHECK(!cloud.Has("docs/sub"));
		CHECK(!cloud.Has("docs/sub/a.txt"));
		CHECK(engine.Sync(local, state.String(), result) == B_OK);
		CHECK(!cloud.Has("docs"));

		cloud.PutFile("Notes.txt", "cloud");
		write_text(local, "notes.txt", "local");
		CHECK(engine.Sync(local, state.String(), result) == B_OK);
		CHECK(cloud.Read("Notes.txt") == "cloud");
		CHECK(!cloud.Has("notes.txt"));
		CHECK(read_text(local, "Notes.txt") == "cloud");
		CHECK(read_text(local, "notes (conflict).txt") == "local");
		CHECK(cloud.Read("notes (conflict).txt") == "local");
	} else {
		DropboxProvider remote(cloud, oauth, sleep, log, "Cloud");
		SyncEngine engine(remote);
		write_text(local, "note.txt", "dropbox");
		CHECK(engine.Sync(local, state.String(), result) == B_OK);
		CHECK(cloud.Read("note.txt") == "dropbox");
		unlink((BString(local) << "/note.txt").String());
		CHECK(engine.Sync(local, state.String(), result) == B_OK);
		CHECK(!cloud.Has("note.txt"));
		CHECK(engine.Sync(local, state.String(), result) == B_OK);
		CHECK(!cloud.Has("note.txt"));
		write_text(local, "both.txt", "start");
		CHECK(engine.Sync(local, state.String(), result) == B_OK);
		write_text(local, "both.txt", "local-side");
		cloud.PutFile("both.txt", "remote-side");
		CHECK(engine.Sync(local, state.String(), result) == B_OK);
		CHECK(read_text(local, "both.txt") == "remote-side");
		CHECK(read_text(local, "both (conflict).txt") == "local-side");
		CHECK(cloud.Read("both (conflict).txt") == "local-side");
	}

	BString saved;
	FILE* stateFile = fopen(state.String(), "rb");
	CHECK(stateFile != NULL);
	if (stateFile != NULL) {
		char buffer[4096];
		size_t n = fread(buffer, 1, sizeof(buffer) - 1, stateFile);
		buffer[n] = '\0';
		fclose(stateFile);
		saved = buffer;
	}
	CHECK(saved.FindFirst("access-token") < 0);
	CHECK(saved.FindFirst("refresh-token") < 0);
	CHECK(log.text.FindFirst("access-token") < 0);
	return gFailed == before ? 0 : 1;
}


static void
test_token_memory()
{
	MemoryTokenStore store;
	BMessage token;
	TokenSet(token, "secret-access", "secret-refresh", 10);
	CHECK(store.Save("onedrive", token) == B_OK);
	MemoryTokenStore other;
	BMessage loaded;
	CHECK(other.Load("onedrive", loaded) == B_NAME_NOT_FOUND);
	CHECK(store.Load("onedrive", loaded) == B_OK);
	CHECK(strcmp(TokenAccess(loaded), "secret-access") == 0);
}


int
RunOnlineStorageTests()
{
	gFailed = 0;
	test_sha_and_pkce();
	test_oauth_request();
	test_loopback();
	test_redirect_and_retry();
	test_recorded_providers();
	test_token_memory();
	sync_case("onedrive");
	sync_case("dropbox");
	if (gFailed == 0)
		printf("onlinestorage tests passed\n");
	else
		printf("onlinestorage tests failed: %d\n", gFailed);
	return gFailed == 0 ? 0 : 1;
}
