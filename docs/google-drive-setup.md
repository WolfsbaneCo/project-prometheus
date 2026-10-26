# Setting up Google Drive backup (once, for the project)

Players never do any of this. They press **B** in the library, click **Connect Google Drive** and sign in with their own Google account. For that to work, Prometheus 64 has to be registered with Google once, as one app for the whole project. That registration is what Google shows on its sign-in page ("Project Prometheus wants to access your Google Account").

It takes about ten minutes, in a Google account the project owns. That account can be different from the one whose Drive holds your saves.

## 1. A Google Cloud project

1. Open [console.cloud.google.com](https://console.cloud.google.com) and sign in.
2. Click the project picker at the top, then **New project**. Name it **Project Prometheus** and click **Create**. Make sure it is the selected project afterwards.
3. Open **APIs & Services → Library**, search for **Google Drive API**, open it and click **Enable**.

## 2. What players see when they sign in

Open **Google Auth Platform** (in the menu, or search for "OAuth consent screen"):

1. **Branding:**
   - App name: **Project Prometheus**.
   - User support email: an address of yours.
   - Developer contact: the same.
   - Leave the logo empty. A logo makes Google review the app first.
2. **Audience:** choose **External**, so anyone with a Google account can connect, not only your own Workspace users.
3. **Data access:** click **Add or remove scopes**, tick `.../auth/drive.file` ("See, edit, create and delete only the specific Google Drive files you use with this app") and save. Don't add anything else.
4. **Audience → Publish app → Confirm.** The status must say **In production**.
   - While an app is in "Testing", Google signs everyone out after 7 days, so backups would quietly stop.
   - `drive.file` is a non-sensitive scope, so publishing needs no review.

## 3. The app's ID

1. **Google Auth Platform → Clients → Create client.**
2. Application type: **Desktop app**. Name: **Prometheus 64 for Windows**. Click **Create**.
3. Copy the **Client ID** (it ends in `.apps.googleusercontent.com`) and the **Client secret**.

For a desktop app, Google doesn't treat the client secret as a real secret: it ships inside the program. It still never goes into the repository; GitHub keeps both values.

## 4. Give them to the build

In the GitHub repository, open **Settings → Secrets and variables → Actions → New repository secret** and add two secrets:

- `GOOGLE_CLIENT_ID`: the Client ID.
- `GOOGLE_CLIENT_SECRET`: the Client secret.

The next installer CI builds has Drive backup switched on. Builds without these secrets work normally, and the Backup screen says that build doesn't have it.

## Later: an Android version

Create its client in **this same project** (Clients → Create client → Android). With the `drive.file` permission, Google lets an app see only the files it made itself. Two clients in one project count as one app, so the phone sees the saves the PC backed up. A separate project would not.
