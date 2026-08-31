#include "../window.hh"
#include "../string.hh"
#include "../app.hh"

#include "dialog.hh"

using oro::runtime::string::split;
using oro::runtime::string::join;
using oro::runtime::string::trim;
#if ORO_RUNTIME_PLATFORM_WINDOWS
using oro::runtime::string::convertWStringToString;
#endif
using oro::runtime::app::App;

#if ORO_RUNTIME_PLATFORM_IOS
@implementation OROUIPickerDelegate : NSObject
-  (void) documentPicker: (UIDocumentPickerViewController*) controller
  didPickDocumentsAtURLs: (NSArray<NSURL*>*) urls {
  using namespace oro::runtime;
  Vector<String> paths;
  for (NSURL* url in urls) {
    if (url.isFileURL) {
      paths.push_back(url.path.UTF8String);
    }
  }
  self.dialog->callback(paths);
}

- (void) documentPickerWasCancelled: (UIDocumentPickerViewController*) controller {
  using namespace oro::runtime;
  self.dialog->callback(Vector<String>());
}

-  (void) imagePickerController: (UIImagePickerController*) picker
  didFinishPickingMediaWithInfo: (NSDictionary<UIImagePickerControllerInfoKey, id>*) info {
  using namespace oro::runtime;
  NSURL* mediaURL = info[UIImagePickerControllerMediaURL];
  NSURL* imageURL = info[UIImagePickerControllerImageURL];
  Vector<String> paths;

  if (mediaURL != nullptr) {
    paths.push_back(mediaURL.path.UTF8String);
  } else {
    paths.push_back(imageURL.path.UTF8String);
  }

  [picker dismissViewControllerAnimated: YES completion: nullptr];
  self.dialog->callback(paths);
}

- (void) imagePickerControllerDidCancel: (UIImagePickerController*) picker {
  using namespace oro::runtime;
  self.dialog->callback(Vector<String>());
}
@end
#endif

#if ORO_RUNTIME_PLATFORM_MACOS
@interface OROSharingServiceBridge : NSObject<NSSharingServicePickerDelegate, NSSharingServiceDelegate>
@property (nonatomic) oro::runtime::window::Dialog* dialog;
@property (nonatomic, strong) NSArray* items;
@property (nonatomic, copy) NSString* subject;
@property (nonatomic, assign) BOOL finished;
- (instancetype) initWithDialog: (oro::runtime::window::Dialog*) dialog
                          items: (NSArray*) items
                        subject: (NSString*) subject;
@end

@implementation OROSharingServiceBridge
- (instancetype) initWithDialog: (oro::runtime::window::Dialog*) dialog
                          items: (NSArray*) items
                        subject: (NSString*) subject {
  if ((self = [super init])) {
    self.dialog = dialog;
    self.items = items;
    self.subject = [subject copy];
    self.finished = NO;
  }
  return self;
}

- (NSArray<NSSharingService*>*) sharingServicePicker: (NSSharingServicePicker*) sharingServicePicker
                             sharingServicesForItems: (NSArray*) items
                          proposedSharingServices: (NSArray<NSSharingService*>*) proposedSharingServices {
  for (NSSharingService* service in proposedSharingServices) {
    service.delegate = self;
    if (self.subject.length > 0) {
      service.subject = self.subject;
    }
  }

  return proposedSharingServices;
}

- (void) sharingServicePicker: (NSSharingServicePicker*) sharingServicePicker
        didChooseSharingService: (NSSharingService*) service {
  service.delegate = self;
  if (self.subject.length > 0) {
    service.subject = self.subject;
  }
}

- (void) sharingServicePickerDidFinish: (NSSharingServicePicker*) sharingServicePicker {
  [self complete: NO error: nil];
}

- (void) sharingService: (NSSharingService*) sharingService
          didShareItems: (NSArray*) items {
  [self complete: YES error: nil];
}

- (void) sharingService: (NSSharingService*) sharingService
    didFailToShareItems: (NSArray*) items
                      error: (NSError*) error {
  [self complete: NO error: error];
}

- (void) complete: (BOOL) success error: (NSError*) error {
  if (self.finished) {
    return;
  }

  self.finished = YES;

  auto dialog = self.dialog;
  if (!dialog) {
    return;
  }

  const auto callback = dialog->shareCallback;
  dialog->shareCallback = nullptr;

  if (callback) {
    oro::runtime::String reason;
    if (error && error.localizedDescription) {
      reason = error.localizedDescription.UTF8String;
    }
    callback(success, reason);
  }

  if (dialog->shareBridge != nullptr) {
    id retained = (__bridge_transfer id) dialog->shareBridge;
    dialog->shareBridge = nullptr;
    (void) retained;
  }
}
@end
#endif

namespace oro::runtime::window {
  Dialog::Dialog (Window* window)
    : window(window) {
  #if ORO_RUNTIME_PLATFORM_IOS
    this->uiPickerDelegate = [OROUIPickerDelegate new];
    this->uiPickerDelegate.dialog = this;
  #endif
  }

  Dialog::~Dialog () {
  #if ORO_RUNTIME_PLATFORM_IOS
    #if !__has_feature(objc_arc)
    [this->uiPickerDelegate release];
    #endif
      this->uiPickerDelegate = nullptr;
  #endif

  #if ORO_RUNTIME_PLATFORM_MACOS
    if (this->shareBridge != nullptr) {
      (__bridge_transfer id) this->shareBridge;
      this->shareBridge = nullptr;
    }
  #endif
  }

  bool Dialog::showSaveFilePicker (
    const FileSystemPickerOptions& options,
    const ShowCallback callback
  ) {
    return this->showFileSystemPicker({
      .prefersDarkMode = options.prefersDarkMode,
      .directories = false,
      .multiple = false,
      .files = true,
      .type = FileSystemPickerOptions::Type::Save,
      .contentTypes = options.contentTypes,
      .defaultName = options.defaultName,
      .defaultPath = options.defaultPath,
      .title = options.title
    }, callback);
  }

  bool Dialog::showOpenFilePicker (
    const FileSystemPickerOptions& options,
    const ShowCallback callback
  ) {
    return this->showFileSystemPicker({
      .prefersDarkMode = options.prefersDarkMode,
      .directories = false,
      .multiple = options.multiple,
      .files = true,
      .type = FileSystemPickerOptions::Type::Open,
      .contentTypes = options.contentTypes,
      .defaultName = options.defaultName,
      .defaultPath = options.defaultPath,
      .title = options.title
    }, callback);
  }

  bool Dialog::showDirectoryPicker (
    const FileSystemPickerOptions& options,
    const ShowCallback callback
  ) {
    return this->showFileSystemPicker({
      .prefersDarkMode = options.prefersDarkMode,
      .directories = true,
      .multiple = options.multiple,
      .files = false,
      .type = FileSystemPickerOptions::Type::Open,
      .contentTypes = options.contentTypes,
      .defaultName = options.defaultName,
      .defaultPath = options.defaultPath,
      .title = options.title
    }, callback);
  }

  bool Dialog::showFileSystemPicker (
    const FileSystemPickerOptions& options,
    const ShowCallback callback
  ) {
    const auto isSavePicker = options.type == FileSystemPickerOptions::Type::Save;
    const auto allowDirectories = options.directories;
    const auto allowMultiple = options.multiple;
    const auto allowFiles = options.files;
    const auto defaultName = options.defaultName;
    const auto defaultPath = options.defaultPath;
    const auto title = options.title;
    const auto app = App::sharedApplication();

    Vector<String> paths;

    this->callback = callback;

  #if ORO_RUNTIME_PLATFORM_APPLE
    // state
    NSMutableArray<UTType *>* contentTypes = [NSMutableArray new];
    NSString* suggestedFilename = nullptr;
    NSURL* directoryURL = nullptr;
    bool prefersMedia = false;

    if (defaultName.size() > 0 && defaultPath.size() == 0) {
      directoryURL = [NSURL fileURLWithPath: @(defaultName.c_str())];
    } else if (defaultPath.size() > 0) {
      directoryURL = [NSURL fileURLWithPath: @(defaultPath.c_str())];
    }

    if (allowDirectories) {
      [contentTypes addObject: UTTypeFolder];
    }

    if (allowFiles) {
      // <mime>:<ext>,<ext>|<mime>:<ext>|...
      for (const auto& contentTypeSpec : split(options.contentTypes, "|")) {
        const auto parts = split(contentTypeSpec, ":");
        const auto mime = trim(parts[0]);
        const auto classes = split(mime, "/");
        UTType* supertype = nullptr;

        // malformed MIME
        if (classes.size() != 2) {
          continue;
        }

        if (classes[0] == "audio") {
          supertype = UTTypeAudio;
          prefersMedia = true;
        } else if (classes[0] == "font") {
          supertype = UTTypeFont;
          prefersMedia = false;
        } else if (classes[0] == "image") {
          supertype = UTTypeImage;
          prefersMedia = true;
        } else if (classes[0] == "text") {
          supertype = UTTypeText;
          prefersMedia = false;
        } else if (classes[0] == "video") {
          supertype = UTTypeVideo;
          prefersMedia = true;
        } else if (classes[0] == "*") {
          supertype = UTTypeData;
        } else {
          supertype = UTTypeCompositeContent;
        }

        // any file extension such that its mime type corresponds to <mime>
        if (parts.size() == 1) {
          if (classes[1] == "*") {
            [contentTypes addObject: supertype];
          } else {
            [contentTypes
              addObjectsFromArray: [UTType
                    typesWithTag: @(mime.c_str())
                        tagClass: UTTagClassMIMEType
                conformingToType: supertype
              ]
            ];
          }
        }

        // given file extensions for a given mime type
        if (parts.size() == 2) {
          const auto extensions = split(parts[1], ",");

          for (const auto& extension : extensions) {
            auto types = [UTType
                  typesWithTag: @(extension.c_str())
                      tagClass: UTTagClassFilenameExtension
              conformingToType: supertype
            ];

            [contentTypes addObjectsFromArray: types];
          }
        }
      }

      if (contentTypes.count == 0 && !allowDirectories) {
        [contentTypes addObject: UTTypeContent];
      }
    }

  #if ORO_RUNTIME_PLATFORM_IOS
    UIWindow* window = nullptr;

    if (this->window) {
      window = this->window->window;
    } else if (@available(iOS 15.0, *)) {
      auto scene = (UIWindowScene*) UIApplication.sharedApplication.connectedScenes.allObjects.firstObject;
      window = scene.windows.lastObject;
    } else {
      window = UIApplication.sharedApplication.windows.lastObject;
    }

    if (prefersMedia) {
      auto picker = [UIImagePickerController new];
      NSMutableArray<NSString*>* mediaTypes = [NSMutableArray new];

      picker.delegate = this->uiPickerDelegate;

      [window.rootViewController
        presentViewController: picker
                     animated: YES
                   completion: nullptr
      ];
    } else {
      auto picker = [UIDocumentPickerViewController.alloc
        initForOpeningContentTypes: contentTypes
      ];

      picker.allowsMultipleSelection = allowMultiple ? YES : NO;
      picker.modalPresentationStyle = UIModalPresentationFormSheet;
      picker.directoryURL = directoryURL;
      picker.delegate = this->uiPickerDelegate;

      [window.rootViewController
        presentViewController: picker
                     animated: YES
                   completion: nullptr
      ];
    }

    return true;
  #else
    NSAutoreleasePool* pool = [NSAutoreleasePool new];

    // dialogs
    NSSavePanel* saveDialog = nullptr;
    NSOpenPanel* openDialog = nullptr;

    if (isSavePicker) {
      saveDialog = [NSSavePanel savePanel];
      saveDialog.allowedContentTypes = contentTypes;
    } else {
      openDialog = [NSOpenPanel openPanel];
      openDialog.allowedContentTypes = contentTypes;
    }

    if (isSavePicker) {
      [saveDialog setCanCreateDirectories: YES];
    } else {
      [openDialog setCanCreateDirectories: YES];
    }

    if (allowDirectories == true && isSavePicker == false) {
      [openDialog setCanChooseDirectories: YES];
    }

    if (isSavePicker == false) {
      [openDialog setCanChooseFiles: allowFiles ? YES : NO];
    }

    if ((isSavePicker == false || allowDirectories == true) && allowMultiple == true) {
      if (openDialog != nullptr) {
        [openDialog setAllowsMultipleSelection: YES];
      }
    }

    if (defaultName.size() > 0) {
      suggestedFilename = @(defaultName.c_str());
    } else if (defaultName.size() == 0 && defaultPath.size() > 0 && directoryURL != nullptr) {
      suggestedFilename = directoryURL.lastPathComponent;
    }

    if (directoryURL != nullptr) {
      if (isSavePicker) {
        [saveDialog setDirectoryURL: directoryURL];
        [saveDialog setNameFieldStringValue: suggestedFilename];
      } else {
        [openDialog setDirectoryURL: directoryURL];
        [openDialog setNameFieldStringValue: suggestedFilename];
      }
    }

    if (title.size() > 0) {
      if (isSavePicker) {
        [saveDialog setTitle: @(title.c_str())];
      } else {
        [openDialog setTitle: @(title.c_str())];
      }
    }

    if (saveDialog != nullptr) {
      if ([saveDialog runModal] == NSModalResponseOK) {
        paths.push_back(saveDialog.URL.path.UTF8String);
      }
    } else if ([openDialog runModal] == NSModalResponseOK) {
      for (NSURL* url in openDialog.URLs) {
        if (url.isFileURL) {
          paths.push_back(url.path.UTF8String);
        }
      }
    }

    [pool release];
    if (paths.size() == 0) {
      return false;
    }
    this->window->bridge->dispatch([=] () {
      callback(paths);
    });
    return true;
  #endif
  #elif defined(__linux__) && !defined(__ANDROID__)
    const guint SELECT_RESPONSE = 0;
    GtkFileChooserAction action;
    GtkFileChooser *chooser;
    GtkWidget *dialog;

    GtkFileFilterFunc wildcardFilter = [](const GtkFileFilterInfo* info, gpointer userData) -> gboolean {
      if (info != nullptr && info->mime_type != nullptr) {
        const auto mimeType = String(info->mime_type);
        if (userData != nullptr) {
          const auto targetMimeType = String((char*) userData);
          if (mimeType.starts_with(targetMimeType)) {
            return TRUE;
          }
        }
      }

      return false;
    }; // NOLINT(readability/braces)

    g_object_set(
      gtk_settings_get_default(),
      "gtk-application-prefer-dark-theme",
      options.prefersDarkMode,
      nullptr
    );

    if (isSavePicker) {
      action = GTK_FILE_CHOOSER_ACTION_SAVE;
    } else {
      action = GTK_FILE_CHOOSER_ACTION_OPEN;
    }

    if (!allowFiles && allowDirectories) {
      action = (GtkFileChooserAction) (action | GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER);
    }

    String dialogTitle = isSavePicker ? "Save File" : "Open File";
    if (title.size() > 0) {
      dialogTitle = title;
    }

    dialog = gtk_file_chooser_dialog_new(
      dialogTitle.c_str(),
      nullptr,
      action,
      "_Cancel",
      GTK_RESPONSE_CANCEL,
      nullptr
    );

    for (const auto& contentTypeSpec : split(options.contentTypes, "|")) {
      const auto parts = split(contentTypeSpec, ":");
      const auto mime = trim(parts[0]);
      const auto classes = split(mime, "/");

      // malformed MIME
      if (classes.size() != 2) {
        continue;
      }

      auto filter = gtk_file_filter_new();
      Set<String> filterExtensionPatterns;

    #define MAKE_FILTER(userData)                                              \
      gtk_file_filter_add_custom(                                              \
        filter,                                                                \
        GTK_FILE_FILTER_MIME_TYPE,                                             \
        wildcardFilter,                                                        \
        (gpointer) userData,                                                   \
        NULL                                                                   \
      );

      if (classes[1] != "*") {
        if (classes[0] == "audio") {
          MAKE_FILTER("audio");
        } else if (classes[0] == "font") {
          MAKE_FILTER("font");
        } else if (classes[0] == "image") {
          MAKE_FILTER("image");
        } else if (classes[0] == "text") {
          MAKE_FILTER("text");
        } else if (classes[0] == "video") {
          MAKE_FILTER("video");
        }
      } else {
        gtk_file_filter_add_mime_type(filter, mime.c_str());
      }

      // given file extensions for a given mime type
      if (parts.size() == 2) {
        const auto extensions = split(parts[1], ",");

        for (const auto& extension : extensions) {
          const auto pattern = (
            String("*") +
            (!extension.starts_with(".") ? "." : "") +
            extension
          );

          filterExtensionPatterns.insert(pattern);

          gtk_file_filter_add_pattern(filter, pattern.c_str());
        }
      }

      gtk_file_filter_set_name(
        filter,
        join(filterExtensionPatterns, ", ").c_str()
      );

      gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(dialog), filter);
      // `gtk_file_chooser_add_filter` sinks the floating reference.
    }

    chooser = GTK_FILE_CHOOSER(dialog);

    if (!allowDirectories) {
      if (isSavePicker) {
        gtk_dialog_add_button(GTK_DIALOG(dialog), "_Save", GTK_RESPONSE_ACCEPT);
      } else {
        gtk_dialog_add_button(GTK_DIALOG(dialog), "_Open", GTK_RESPONSE_ACCEPT);
      }
    }

    if (allowMultiple || allowDirectories) {
      gtk_dialog_add_button(GTK_DIALOG(dialog), "Select", SELECT_RESPONSE);
    }

    gtk_file_chooser_set_do_overwrite_confirmation(chooser, true);

    if ((!isSavePicker || allowDirectories) && allowMultiple) {
      gtk_file_chooser_set_select_multiple(chooser, true);
    }

    if (defaultPath.size() > 0) {
      auto status = fs::status(defaultPath);

      if (fs::exists(status)) {
        if (fs::is_directory(status)) {
          gtk_file_chooser_set_current_folder(chooser, defaultPath.c_str());
        } else {
          gtk_file_chooser_set_filename(chooser, defaultPath.c_str());
        }
      }
    }

    if (defaultName.size() > 0) {
      if ((!allowFiles && allowDirectories) || isSavePicker) {
        gtk_file_chooser_set_current_name(chooser, defaultName.c_str());
      } else {
        gtk_file_chooser_set_current_folder(chooser, defaultName.c_str());
      }
    }

    guint response = gtk_dialog_run(GTK_DIALOG(dialog));

    // filters are owned by chooser; nothing to clear here

    if (response != GTK_RESPONSE_ACCEPT && response != SELECT_RESPONSE) {
      gtk_widget_destroy(dialog);
      return false;
    }

    // TODO: validate multi-select selections and ensure chooser results are complete

    while (gtk_events_pending()) {
      gtk_main_iteration();
    }

    GSList* filenames = gtk_file_chooser_get_filenames(chooser);
    GSList* iter = filenames;

    for (int i = 0; iter != nullptr; ++i) {
      const auto filename = (const char*) iter->data;
      paths.push_back(filename);
      g_free(const_cast<char*>(reinterpret_cast<const char*>(filename)));
      iter = iter->next;
    }

    g_slist_free(filenames);
    gtk_widget_destroy(GTK_WIDGET(dialog));
    this->window->bridge->dispatch([=] () {
      callback(paths);
    });
    return true;
  #elif ORO_RUNTIME_PLATFORM_WINDOWS
    IShellItemArray *openResults;
    IShellItem *saveResult;
    DWORD dialogOptions;
    DWORD totalResults;
    HRESULT result;

    // the dialogs as a union, because there can only _one_.
    union {
      IFileSaveDialog *save;
      IFileOpenDialog *open;
    } dialog;

    result = CoInitializeEx(
      NULL,
      COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE
    );

    if (FAILED(result)) {
      debug("ERR: CoInitializeEx() failed in 'showFileSystemPicker()'");
      return false;
    }

    // create IFileDialog instance (IFileOpenDialog or IFileSaveDialog)
    if (isSavePicker) {
      result = CoCreateInstance(
        CLSID_FileSaveDialog,
        NULL,
        CLSCTX_ALL,
        IID_PPV_ARGS(&dialog.save)
      );

      if (FAILED(result)) {
        debug("ERR: CoCreateInstance() failed in 'showFileSystemPicker()'");
        CoUninitialize();
        return false;
      }
    } else {
      result = CoCreateInstance(
        CLSID_FileOpenDialog,
        NULL,
        CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(&dialog.open)
      );

      if (FAILED(result)) {
        debug("ERR: CoCreateInstance() failed in 'showFileSystemPicker()'");
        CoUninitialize();
        return false;
      }
    }

    if (isSavePicker) {
      result = dialog.save->GetOptions(&dialogOptions);
    } else {
      result = dialog.open->GetOptions(&dialogOptions);
    }

    if (FAILED(result)) {
      debug("ERR: IFileDialog::GetOptions() failed in 'showFileSystemPicker()'");
      CoUninitialize();
      return false;
    }

    if (allowDirectories == true && allowFiles == false) {
      if (isSavePicker) {
        result = dialog.save->SetOptions(dialogOptions | FOS_PICKFOLDERS);
      } else {
        result = dialog.open->SetOptions(dialogOptions | FOS_PICKFOLDERS);
      }

      if (FAILED(result)) {
        debug("ERR: IFileDialog::SetOptions(FOS_PICKFOLDERS) failed in 'showFileSystemPicker()'");
        CoUninitialize();
        return false;
      }
    }

    if ((!isSavePicker || (!isSavePicker && allowDirectories)) && allowMultiple) {
      result = dialog.open->SetOptions(dialogOptions | FOS_ALLOWMULTISELECT);

      if (FAILED(result)) {
        debug("ERR: IFileDialog::SetOptions(FOS_ALLOWMULTISELECT) failed in 'showFileSystemPicker()'");
        CoUninitialize();
        return false;
      }
    }

    if (!defaultPath.empty()) {
      IShellItem *defaultFolder;

      auto normalizedDefaultPath = defaultPath;
      std::replace(normalizedDefaultPath.begin(), normalizedDefaultPath.end(), '/', '\\');
      result = SHCreateItemFromParsingName(
        WString(normalizedDefaultPath.begin(), normalizedDefaultPath.end()).c_str(),
        NULL,
        IID_PPV_ARGS(&defaultFolder)
      );

      if (FAILED(result)) {
        debug("ERR: SHCreateItemFromParsingName() failed in 'showFileSystemPicker()'");
        CoUninitialize();
        return false;
      }

      if (isSavePicker) {
        result = dialog.save->SetDefaultFolder(defaultFolder);
      } else {
        result = dialog.open->SetDefaultFolder(defaultFolder);
      }

      if (FAILED(result)) {
        debug("ERR: IFileDialog::SetDefaultFolder() failed in 'showFileSystemPicker()'");
        CoUninitialize();
        return false;
      }
    }

    if (!title.empty()) {
      if (isSavePicker) {
        result = dialog.save->SetTitle(
          WString(title.begin(), title.end()).c_str()
        );
      } else {
        result = dialog.open->SetTitle(
          WString(title.begin(), title.end()).c_str()
        );
      }

      if (FAILED(result)) {
        debug("ERR: IFileDialog::SetTitle() failed in 'showFileSystemPicker()'");
        CoUninitialize();
        return false;
      }
    }

    if (!defaultName.empty()) {
      if (isSavePicker) {
        result = dialog.save->SetFileName(
          WString(defaultName.begin(), defaultName.end()).c_str()
        );
      } else {
        result = dialog.open->SetFileName(
          WString(defaultName.begin(), defaultName.end()).c_str()
        );
      }

      if (FAILED(result)) {
        debug("ERR: IFileDialog::SetFileName() failed in 'showFileSystemPicker()'");
        CoUninitialize();
        return false;
      }
    }

    if (isSavePicker) {
      result = dialog.save->Show(NULL);
    } else {
      result = dialog.open->Show(NULL);
    }

    if (FAILED(result)) {
      debug("ERR: IFileDialog::Show() failed in 'showFileSystemPicker()'");
      CoUninitialize();
      return false;
    }

    if (isSavePicker) {
      result = dialog.save->GetResult(&saveResult);

      if (FAILED(result)) {
        debug("ERR: IFileDialog::GetResult() failed in 'showFileSystemPicker()'");
        CoUninitialize();
        return false;
      }
    } else {
      result = dialog.open->GetResults(&openResults);

      if (FAILED(result)) {
        debug("ERR: IFileDialog::GetResults() failed in 'showFileSystemPicker()'");
        CoUninitialize();
        return false;
      }
    }

    if (FAILED(result)) {
      debug("ERR: IFileDialog::Show() failed in 'showFileSystemPicker()'");
      CoUninitialize();
      callback(paths);
      return false;
    }

    if (isSavePicker) {
      LPWSTR buf;

      result = saveResult->GetDisplayName(SIGDN_DESKTOPABSOLUTEPARSING, &buf);

      if (FAILED(result)) {
        debug("ERR: IShellItem::GetDisplayName() failed in 'showFileSystemPicker()'");
        CoUninitialize();
        return false;
      }

      paths.push_back(convertWStringToString(WString(buf)));
      saveResult->Release();

      CoTaskMemFree(buf);
    } else {
      openResults->GetCount(&totalResults);

      if (FAILED(result)) {
        debug("ERR: IShellItemArray::GetCount() failed in 'showFileSystemPicker()'");
        CoUninitialize();
        return false;
      }

      for (DWORD i = 0; i < totalResults; i++) {
        IShellItem *path;
        LPWSTR buf;

        result = openResults->GetItemAt(i, &path);

        if (FAILED(result)) {
          debug("ERR: IShellItemArray::GetItemAt() failed in 'showFileSystemPicker()'");
          CoUninitialize();
          return false;
        }

        result = path->GetDisplayName(SIGDN_DESKTOPABSOLUTEPARSING, &buf);

        if (FAILED(result)) {
          debug("ERR: IShellItem::GetDisplayName() failed in 'showFileSystemPicker()'");
          CoUninitialize();
          return false;
        }

        paths.push_back(convertWStringToString(WString(buf)));
        path->Release();
        CoTaskMemFree(buf);
      }
    }

    if (isSavePicker) {
      dialog.save->Release();
    } else {
      dialog.open->Release();
    }

    if (!isSavePicker) {
      openResults->Release();
    }

    CoUninitialize();
    app->dispatch([=]() {
      callback(paths);
    });
    return true;
  #elif ORO_RUNTIME_PLATFORM_ANDROID
    const auto attachment = android::JNIEnvironmentAttachment(app->runtime.android.jvm);
    const auto dialog = CallObjectClassMethodFromAndroidEnvironment(
      attachment.env,
      app->runtime.android.activity,
      "getDialog",
      "()Loro/runtime/window/Dialog;"
    );

    // construct the mime types into a packed string
    String mimeTypes;
    // <mime>:<ext>,<ext>|<mime>:<ext>|...
    for (const auto& contentTypeSpec : split(options.contentTypes, "|")) {
      const auto parts = split(contentTypeSpec, ":");
      const auto mime = trim(parts[0]);
      const auto classes = split(mime, "/");
      if (classes.size() == 2) {
        if (mimeTypes.size() == 0) {
          mimeTypes = mime;
        } else {
          mimeTypes += "|" + mime;
        }
      }
    }

    // we'll set the pointer from this instance in this call so
    // the `onResults` can reinterpret the `jlong` back into a `Dialog*`
    CallVoidClassMethodFromAndroidEnvironment(
      attachment.env,
      dialog,
      "showFileSystemPicker",
      "(Ljava/lang/String;ZZZJ)V",
      attachment.env->NewStringUTF(mimeTypes.c_str()),
      allowDirectories,
      allowMultiple,
      allowFiles,
      reinterpret_cast<jlong>(this)
    );

    return true;
  #endif

    return false;
  }

  bool Dialog::share (const ShareOptions& options, const ShareCallback cb) {
  #if ORO_RUNTIME_PLATFORM_APPLE
    this->shareCallback = cb;

    NSMutableArray* items = [NSMutableArray new];
    bool hasItem = false;

    if (options.text.size() > 0) {
      [items addObject: @(options.text.c_str())];
      hasItem = true;
    }

    if (options.url.size() > 0) {
      auto url = [NSURL URLWithString: @(options.url.c_str())];
      if (url) {
        [items addObject: url];
        hasItem = true;
      }
    }

    if (!hasItem && options.title.size() > 0) {
      [items addObject: @(options.title.c_str())];
      hasItem = true;
    }

    if (!hasItem) {
      this->shareCallback = nullptr;
    #if !__has_feature(objc_arc)
      [items release];
    #endif
      return false;
    }

  #if ORO_RUNTIME_PLATFORM_IOS
    UIWindow* window = nullptr;

    if (this->window) {
      window = this->window->window;
    } else if (@available(iOS 15.0, *)) {
      auto scene = (UIWindowScene*) UIApplication.sharedApplication.connectedScenes.allObjects.firstObject;
      window = scene.windows.lastObject;
    } else {
      window = UIApplication.sharedApplication.windows.lastObject;
    }

    if (window == nullptr) {
      this->shareCallback = nullptr;
    #if !__has_feature(objc_arc)
      [items release];
    #endif
      return false;
    }

    UIViewController* presenter = window.rootViewController;
    while (presenter && presenter.presentedViewController) {
      presenter = presenter.presentedViewController;
    }

    if (presenter == nullptr) {
      this->shareCallback = nullptr;
    #if !__has_feature(objc_arc)
      [items release];
    #endif
      return false;
    }

    UIActivityViewController* controller = [[UIActivityViewController alloc]
      initWithActivityItems: items
      applicationActivities: nil
    ];

    if (options.title.size() > 0) {
      [controller setValue: @(options.title.c_str()) forKey: @"subject"];
    }

    __block Dialog* dialog = this;

    controller.completionWithItemsHandler = ^(
      UIActivityType activityType,
      BOOL completed,
      NSArray* returnedItems,
      NSError* activityError
    ) {
      (void) activityType;
      (void) returnedItems;

      const auto callback = dialog->shareCallback;
      dialog->shareCallback = nullptr;

      if (!callback) {
        return;
      }

      if (activityError) {
        oro::runtime::String reason;
        if (activityError.localizedDescription) {
          reason = activityError.localizedDescription.UTF8String;
        }
        callback(false, reason);
      } else if (!completed) {
        callback(false, oro::runtime::String());
      } else {
        callback(true, oro::runtime::String());
      }
    }; // NOLINT(readability/braces)

    if (controller.popoverPresentationController) {
      controller.popoverPresentationController.sourceView = presenter.view;
      controller.popoverPresentationController.sourceRect = CGRectMake(
        CGRectGetMidX(presenter.view.bounds),
        CGRectGetMidY(presenter.view.bounds),
        0,
        0
      );
      controller.popoverPresentationController.permittedArrowDirections = 0;
    }

    [presenter presentViewController: controller animated: YES completion: nil];

  #if !__has_feature(objc_arc)
    [controller release];
    [items release];
  #endif

    return true;
  #else
    NSString* subject = options.title.size() > 0 ? @(options.title.c_str()) : nil;
    auto bridge = [[OROSharingServiceBridge alloc] initWithDialog: this items: items subject: subject];
    this->shareBridge = (__bridge_retained void*) bridge;

    NSView* view = this->window ? this->window->window.contentView : nullptr;
    if (view == nullptr) {
      this->shareCallback = nullptr;

      if (this->shareBridge != nullptr) {
        id retained = (__bridge_transfer id) this->shareBridge;
        this->shareBridge = nullptr;
        (void) retained;
      }

    #if !__has_feature(objc_arc)
      [bridge release];
      [items release];
    #endif

      return false;
    }

    NSSharingServicePicker* picker = [[NSSharingServicePicker alloc] initWithItems: items];
    picker.delegate = bridge;
    [picker showRelativeToRect: view.bounds ofView: view preferredEdge: NSMaxYEdge];

  #if !__has_feature(objc_arc)
    [picker release];
    [items release];
  #endif

    return true;
  #endif

  #elif ORO_RUNTIME_PLATFORM_ANDROID
    this->shareCallback = cb;
    const auto app = App::sharedApplication();

    if (app == nullptr) {
      this->shareCallback = nullptr;
      return false;
    }

    const auto attachment = android::JNIEnvironmentAttachment(app->runtime.android.jvm);
    const auto dialog = CallObjectClassMethodFromAndroidEnvironment(
      attachment.env,
      app->runtime.android.activity,
      "getDialog",
      "()Loro/runtime/window/Dialog;"
    );

    if (dialog == nullptr) {
      this->shareCallback = nullptr;
      return false;
    }

    jstring title = options.title.size() > 0 ? attachment.env->NewStringUTF(options.title.c_str()) : nullptr;
    jstring text = options.text.size() > 0 ? attachment.env->NewStringUTF(options.text.c_str()) : nullptr;
    jstring url = options.url.size() > 0 ? attachment.env->NewStringUTF(options.url.c_str()) : nullptr;

    CallVoidClassMethodFromAndroidEnvironment(
      attachment.env,
      dialog,
      "share",
      "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;J)V",
      title,
      text,
      url,
      reinterpret_cast<jlong>(this)
    );

    if (title) attachment.env->DeleteLocalRef(title);
    if (text) attachment.env->DeleteLocalRef(text);
    if (url) attachment.env->DeleteLocalRef(url);

    return true;
  #else
    (void) options;
    (void) cb;
    return false;
  #endif
  }
}

#if ORO_RUNTIME_PLATFORM_ANDROID
extern "C" {
  void ANDROID_EXTERNAL(window, Dialog, onResults) (
    JNIEnv* env,
    jobject self,
    jlong pointer,
    jobjectArray results
  ) {
    using namespace oro::runtime;

    const auto dialog = reinterpret_cast<window::Dialog*>(pointer);

    if (!dialog) {
      return ANDROID_THROW(
        env,
        "Missing 'Dialog' in results callback from 'showFileSystemPicker'"
      );
    }

    if (dialog->callback == nullptr) {
      return ANDROID_THROW(
        env,
        "Missing 'Dialog' callback in results callback from 'showFileSystemPicker'"
      );
    }

    const auto attachment = android::JNIEnvironmentAttachment(dialog->window->bridge->context.android.jvm);
    const auto length = attachment.env->GetArrayLength(results);

    Vector<String> paths;

    for (int i = 0; i < length; ++i) {
      const auto uri = (jstring) attachment.env->GetObjectArrayElement(results, i);
      if (uri) {
        const auto string = android::StringWrap(attachment.env, CallObjectClassMethodFromAndroidEnvironment(
          attachment.env,
          uri,
          "toString",
          "()Ljava/lang/String;"
        )).str();

        paths.push_back(string);
      }
    }

    const auto callback = dialog->callback;
    dialog->callback = nullptr;
    callback(paths);
  }

  void ANDROID_EXTERNAL(window, Dialog, onShareResult) (
    JNIEnv* env,
    jobject self,
    jlong pointer,
    jboolean success,
    jstring errorMessage
  ) {
    using namespace oro::runtime;

    const auto dialog = reinterpret_cast<window::Dialog*>(pointer);

    if (dialog == nullptr) {
      return ANDROID_THROW(env, "Missing 'Dialog' in share callback from 'share()'");
    }

    if (dialog->shareCallback == nullptr) {
      return;
    }

    oro::runtime::String message;

    if (errorMessage != nullptr) {
      message = android::StringWrap(env, errorMessage).str();
    }

    const auto callback = dialog->shareCallback;
    dialog->shareCallback = nullptr;
    callback(success == JNI_TRUE, message);
  }
}
#endif
