#include "scannerdevice.h"
#include <QDebug>
#include <QFile>
#include <QApplication>   // For applicationDirPath
#include <QDir>   // For separator
#include <QProcess>   // For QProcess
#include <QPainter>
#include <QDateTime>
#include <QThread>

// --- Dummy definitions for Image/advance if not available elsewhere ---
// Replace these with your actual definitions if they exist elsewhere
#ifndef _WIN32
bool ScannerDevice::advance(struct ScannerDevice::Image *im)
{
    // Dummy implementation - replace with your actual 'advance' logic
    qWarning() << "Warning: Dummy 'advance' function called. Implement required logic.";
    if (!im || !im->data) return false;   // Indicate failure if im or data is null

    // Basic placeholder logic:
    im->x++;
    if (im->x >= im->width) {
        im->x = 0;
        im->y++;
        // Minimal reallocation simulation check (needs proper implementation)
        if (im->y >= im->height) {
            // Need reallocation logic here if height grows dynamically
            qWarning() << "Image buffer full in dummy 'advance'. Reallocation needed.";
            // Here, your actual advance might realloc 'im->data' and update 'im->height'.
            // If reallocation fails, return false. If successful, return true.
            return false;   // Indicate failure (buffer full and no reallocation)
        }
    }
    // Indicate success if buffer is still valid (potentially after reallocation in real code)
    return true;
}
// Dummy implementation for the write_png_header function referenced in scan_it
// You will need the actual implementation.
static void write_png_header(SANE_Frame format, int width, int height, int depth, FILE *ofp, png_structp *png_ptr_p, png_infop *info_ptr_p)
{
    qWarning() << "Warning: Dummy 'write_png_header' function called.";
    // Minimal setup to avoid crashes in png_write_row/end if called
    *png_ptr_p = png_create_write_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);
    if (!*png_ptr_p) return;
    *info_ptr_p = png_create_info_struct(*png_ptr_p);
    if (!*info_ptr_p) {
        png_destroy_write_struct(png_ptr_p, nullptr);
        return;
    }
    if (setjmp(png_jmpbuf(*png_ptr_p))) {   // Basic error handling setup
        png_destroy_write_struct(png_ptr_p, info_ptr_p);
        return;
    }
    png_init_io(*png_ptr_p, ofp);
    // Set basic header fields (replace with actual logic from your original code)
    png_set_IHDR(*png_ptr_p, *info_ptr_p, width, height, depth,
                 (format == SANE_FRAME_RGB) ? PNG_COLOR_TYPE_RGB : PNG_COLOR_TYPE_GRAY,
                 PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);
    png_write_info(*png_ptr_p, *info_ptr_p);
}
#endif
// --- End Dummy definitions ---

ScannerDevice::ScannerDevice(QObject *parent)
    : QObject(parent)
{
#ifdef _WIN32
    qWarning() << "SANE scanning is not supported on Windows in this build.";
#endif
}

ScannerDevice::~ScannerDevice()
{
#ifndef _WIN32
    closeDevice();   // Ensure device is closed if open
    if (m_saneInitialized) {
        sane_exit();
        qDebug() << "SANE exited.";
    }
#endif
}

bool ScannerDevice::initializeSane()
{
#ifndef _WIN32
    if (m_saneInitialized) {
        return true;
    }

    SANE_Int version_code;
    SANE_Status status = sane_init(&version_code, NULL);   // Using NULL for auth_callback
    if (status != SANE_STATUS_GOOD) {
        QString errorMsg = QString("Failed to initialize SANE: %1").arg(sane_strstatus(status));
        qWarning() << errorMsg;
        emit scanError(errorMsg);
        return false;
    }
    m_saneInitialized = true;
    qDebug() << "SANE initialized successfully. Version code:" << version_code;
    return true;
#else
    emit scanError("SANE backend not available on this platform.");
    return false;
#endif
}

QStringList ScannerDevice::getAvailableDevices()
{
    QStringList deviceNames;
#ifndef _WIN32
    if (!m_saneInitialized) {
        qDebug() << "SANE not initialized, attempting to initialize...";
        if (!initializeSane()) {
            qWarning() << "Failed to initialize SANE";
            return deviceNames;
        }
    }

    // 添加更详细的SANE版本信息
    SANE_Int version_code;
    sane_init(&version_code, NULL);
    int major = SANE_VERSION_MAJOR(version_code);
    int minor = SANE_VERSION_MINOR(version_code);
    int build = SANE_VERSION_BUILD(version_code);
    qDebug() << "SANE Version:" << major << "." << minor << "." << build;

    // 检查USB设备连接
    QProcess lsusbProcess;
    lsusbProcess.start("lsusb");
    lsusbProcess.waitForFinished(5000);
    QString lsusbOutput = QString::fromUtf8(lsusbProcess.readAllStandardOutput());
    qDebug() << "Connected USB devices:";
    qDebug() << lsusbOutput;

    // 检查是否有saned进程在运行
    QProcess psProcess;
    psProcess.start("ps", QStringList() << "aux" << "|" << "grep" << "saned");
    psProcess.waitForFinished(5000);
    QString psOutput = QString::fromUtf8(psProcess.readAllStandardOutput());
    qDebug() << "SANE daemon processes:";
    qDebug() << psOutput;

    // 检查后端配置
    QDir backendDir("/etc/sane.d/dll.d");
    if (backendDir.exists()) {
        qDebug() << "SANE backend configurations:";
        QStringList entries = backendDir.entryList(QDir::Files);
        for (const QString &entry : entries) {
            qDebug() << " -" << entry;
            
            // 读取每个后端配置文件
            QFile backendFile(backendDir.filePath(entry));
            if (backendFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
                QTextStream in(&backendFile);
                QString fileContent = in.readAll();
                qDebug() << "   Content:" << fileContent;
                backendFile.close();
            }
        }
    }

    // 尝试使用SANE_DEBUG环境变量来获取更多信息
    qputenv("SANE_DEBUG_DLL", "255");
    qputenv("SANE_DEBUG_NET", "255");

    // 尝试手动运行scanimage命令 - 有些系统中SANE库与scanimage使用不同的配置
    QProcess process;
    process.start("scanimage", QStringList() << "-L");
    process.waitForFinished(5000);
    QString output = QString::fromUtf8(process.readAllStandardOutput());
    QString error = QString::fromUtf8(process.readAllStandardError());

    qDebug() << "scanimage -L output:" << output;
    if (!error.isEmpty()) {
        qDebug() << "scanimage -L error:" << error;
    }

    // 检查当前用户是否在scanner组中
    QProcess groupsProcess;
    groupsProcess.start("groups");
    groupsProcess.waitForFinished(5000);
    QString groupsOutput = QString::fromUtf8(groupsProcess.readAllStandardOutput());
    qDebug() << "Current user groups:" << groupsOutput;
    
    if (!groupsOutput.contains("scanner") && !groupsOutput.contains("saned")) {
        qWarning() << "Current user is not in the scanner or saned group!";
        qWarning() << "This may cause permission issues. Add user to these groups with:";
        qWarning() << "sudo usermod -a -G scanner $USER";
        qWarning() << "sudo usermod -a -G saned $USER";
    }

    // 尝试不同的参数调用sane_get_devices
    qDebug() << "Trying sane_get_devices with SANE_TRUE (include network devices)...";
    const SANE_Device **device_list = nullptr;
    SANE_Status status = sane_get_devices(&device_list, SANE_TRUE);
    qDebug() << "sane_get_devices(SANE_TRUE) status:" << sane_strstatus(status);

    if (status != SANE_STATUS_GOOD || !device_list) {
        qDebug() << "Trying sane_get_devices with SANE_FALSE (local only)...";
        status = sane_get_devices(&device_list, SANE_FALSE);
        qDebug() << "sane_get_devices(SANE_FALSE) status:" << sane_strstatus(status);
    }

    if (status != SANE_STATUS_GOOD) {
        QString errorMsg = QString("Failed to get SANE device list: %1").arg(sane_strstatus(status));
        qWarning() << errorMsg;
        emit scanError(errorMsg);
        
        // 如果特定后端没有安装，尝试提供建议
        QProcess dpkgProcess;
        dpkgProcess.start("dpkg", QStringList() << "-l" << "*sane*");
        dpkgProcess.waitForFinished(5000);
        QString dpkgOutput = QString::fromUtf8(dpkgProcess.readAllStandardOutput());
        qDebug() << "Installed SANE packages:";
        qDebug() << dpkgOutput;
        
        if (!dpkgOutput.contains("libsane-extras")) {
            qWarning() << "libsane-extras package not found, which contains additional scanner drivers";
            qWarning() << "Try installing: sudo apt-get install libsane-extras";
        }

        // 如果没有真实设备，添加一个虚拟测试设备
        if (deviceNames.isEmpty()) {
            qDebug() << "Adding a virtual test scanner device";
            deviceNames.append("test:0");
        }
        
        return deviceNames;
    }

    if (!device_list) {
        qWarning() << "Device list is null, but status was GOOD";
        emit scanError("Device list is null");
        
        // 添加虚拟测试设备
        deviceNames.append("test:0");
        return deviceNames;
    }

    qDebug() << "Scanning SANE devices:";
    for (int i = 0; device_list[i] != nullptr; ++i) {
        const SANE_Device *dev = device_list[i];
        if (dev) {
            qDebug() << "Device found:";
            qDebug() << "  Name:" << (dev->name ? dev->name : "null");
            qDebug() << "  Vendor:" << (dev->vendor ? dev->vendor : "null");
            qDebug() << "  Model:" << (dev->model ? dev->model : "null");
            qDebug() << "  Type:" << (dev->type ? dev->type : "null");

            if (dev->name) {
                deviceNames.append(QString::fromUtf8(dev->name));

                // 构建更详细的设备描述
                QString deviceInfo = QString("%1 %2 (%3)")
                                             .arg(dev->vendor ? QString::fromUtf8(dev->vendor) : "Unknown")
                                             .arg(dev->model ? QString::fromUtf8(dev->model) : "Unknown")
                                             .arg(dev->type ? QString::fromUtf8(dev->type) : "Unknown");

                qDebug() << "Added device:" << dev->name << "-" << deviceInfo;
            }
        }
    }

    // 如果找不到设备，添加一个虚拟测试设备
    if (deviceNames.isEmpty()) {
        qDebug() << "No SANE devices found, adding a virtual test device";
        deviceNames.append("test:0");
        
        // 建议一些可能的解决方法
        emit scanError(tr("未找到扫描设备。以下操作可能有帮助:\n"
                        "1. 确保扫描仪已连接并开机\n"
                        "2. 运行命令: sudo gpasswd -a $USER scanner\n"
                        "3. 重新启动SANE: sudo service saned restart\n"
                        "4. 安装所需的驱动包: sudo apt-get install libsane-extras\n"
                        "5. 如果是网络扫描仪，检查网络配置\n"
                        "6. 重新插拔USB连接或重启电脑"));
    }

#else
    emit scanError("SANE backend not available on this platform.");
    deviceNames.append("test:0");   // 在Windows上也添加测试设备
#endif
    return deviceNames;
}

bool ScannerDevice::openDevice(const QString &deviceName)
{
#ifndef _WIN32
    // 如果是测试设备，执行虚拟扫描仪流程
    if (deviceName == "test:0") {
        m_deviceOpen = true;
        m_usingTestDevice = true;
        qDebug() << "Opened virtual test scanner device";
        return true;
    }

    if (!m_saneInitialized) {
        emit scanError("SANE not initialized. Call initializeSane() first.");
        return false;
    }
    if (m_deviceOpen) {
        closeDevice();   // Close previous device if any
    }

    QByteArray deviceNameBytes = deviceName.toUtf8();   // SANE uses const char*
    SANE_Status status = sane_open(deviceNameBytes.constData(), &m_device);
    if (status != SANE_STATUS_GOOD) {
        QString errorMsg = QString("Failed to open SANE device '%1': %2")
                                   .arg(deviceName)
                                   .arg(sane_strstatus(status));
        qWarning() << errorMsg;
        emit scanError(errorMsg);
        m_device = nullptr;
        m_deviceOpen = false;
        return false;
    }

    // Set I/O mode (optional, 0 usually means blocking)
    status = sane_set_io_mode(m_device, SANE_FALSE);   // SANE_FALSE for blocking mode
    if (status != SANE_STATUS_GOOD) {
        qWarning() << "Failed to set SANE I/O mode:" << sane_strstatus(status) << "- proceeding anyway.";
        // Decide if this is critical - often it's not.
    }

    // Optionally get and log parameters
    SANE_Parameters pars;
    status = sane_get_parameters(m_device, &pars);
    if (status != SANE_STATUS_GOOD) {
        qWarning() << "Failed to get initial SANE parameters:" << sane_strstatus(status);
        // Decide if this is critical
    } else {
        qDebug() << "Device" << deviceName << "opened. Format:" << pars.format << " Pixels/Line:" << pars.pixels_per_line;
    }

    m_deviceOpen = true;
    qDebug() << "Device" << deviceName << "opened successfully.";
    return true;

#else
    // 在Windows上使用虚拟扫描仪
    if (deviceName == "test:0") {
        m_deviceOpen = true;
        m_usingTestDevice = true;
        qDebug() << "Opened virtual test scanner device on Windows";
        return true;
    } else {
        emit scanError("SANE backend not available on this platform.");
    }
#endif
    return false;
}

void ScannerDevice::closeDevice()
{
#ifndef _WIN32
    if (m_deviceOpen && m_device) {
        sane_close(m_device);
        qDebug() << "Closed SANE device.";
    }
    m_device = nullptr;
    m_deviceOpen = false;
#endif
}

void ScannerDevice::startScan(const QString &tempOutputFilePath)
{
#ifndef _WIN32
    // 如果是测试设备，生成测试图像
    if (m_usingTestDevice) {
        generateTestImage(tempOutputFilePath);
        return;
    }

    if (!m_deviceOpen || !m_device) {
        emit scanError(tr("扫描仪未打开"));
        return;
    }

    // 获取扫描仪参数
    SANE_Parameters params;
    SANE_Status status = sane_get_parameters(m_device, &params);
    if (status != SANE_STATUS_GOOD) {
        emit scanError(tr("无法获取扫描仪参数: %1").arg(sane_strstatus(status)));
        return;
    }

    qDebug() << "Scan parameters:";
    qDebug() << "Format:" << params.format;
    qDebug() << "Last frame:" << params.last_frame;
    qDebug() << "Lines:" << params.lines;
    qDebug() << "Depth:" << params.depth;
    qDebug() << "Pixels per line:" << params.pixels_per_line;
    qDebug() << "Bytes per line:" << params.bytes_per_line;

    // Start the scan frame
    status = sane_start(m_device);
    if (status != SANE_STATUS_GOOD) {
        emit scanError(tr("启动扫描失败: %1").arg(sane_strstatus(status)));
        return;
    }

    qDebug() << "Scan started. Writing temporary PNG to:" << tempOutputFilePath;

    // Open the output file pointer for scan_it
    QByteArray tempPathBytes = tempOutputFilePath.toLocal8Bit();   // Use local encoding for file paths
    FILE *ofp = fopen(tempPathBytes.constData(), "wb");   // Use "wb" for binary PNG
    if (!ofp) {
        QString errorMsg = QString("Failed to open temporary output file '%1' for writing.").arg(tempOutputFilePath);
        qWarning() << errorMsg;
        emit scanError(errorMsg);
        sane_cancel(m_device);   // Cancel the scan started above
        return;
    }

    // --- Call the core scanning function ---
    status = scan_it(ofp);
    // ---                                ---

    fclose(ofp);   // Close the file pointer

    if (status != SANE_STATUS_GOOD && status != SANE_STATUS_EOF) {
        // EOF is expected at the end of a successful scan, so don't treat it as an error here.
        QString errorMsg = QString("Scan failed during read: %1").arg(sane_strstatus(status));
        qWarning() << errorMsg;
        emit scanError(errorMsg);
        QFile::remove(tempOutputFilePath);   // Clean up temp file on error
        // sane_cancel(m_device); // scan_it might have implicitly cancelled or finished
    } else {
        qDebug() << "scan_it finished with status:" << sane_strstatus(status);
        // Load the QImage from the temporary file
        QImage scannedImage;
        if (scannedImage.load(tempOutputFilePath)) {
            qDebug() << "Successfully loaded scanned image from temp file.";
            emit scanCompleted(scannedImage);
        } else {
            QString errorMsg = QString("Failed to load QImage from temporary file '%1'.").arg(tempOutputFilePath);
            qWarning() << errorMsg;
            emit scanError(errorMsg);
        }
        // Clean up the temporary file
        if (!QFile::remove(tempOutputFilePath)) {
            qWarning() << "Could not remove temporary scan file:" << tempOutputFilePath;
        }
    }

    // Note: sane_cancel or sane_close is implicitly handled by scan_it's cleanup
    // or should be called in closeDevice(). We don't call sane_exit here,
    // leave it for the destructor.

#else
    if (m_usingTestDevice) {
        generateTestImage(tempOutputFilePath);
        return;
    } else {
        emit scanError("SANE backend not available on this platform.");
    }
#endif
}

void ScannerDevice::cancelScan()
{
#ifndef _WIN32
    if (m_deviceOpen && m_device) {
        sane_cancel(m_device);
        qDebug() << "Sent SANE cancel request.";
        // Note: Cancellation might not be immediate or guaranteed.
        // The ongoing sane_read might still complete or return an error status.
    }
#endif
}

// --- scan_it implementation (moved inside the class) ---
#ifndef _WIN32
SANE_Status ScannerDevice::scan_it(FILE *ofp)
{
    // --- This is largely your original scan_it function ---
    // --- Adapted slightly for class context and progress emission ---

    // Constants from original code - adjust if needed
    const int STRIP_HEIGHT = 128;   // Example value, check original context

    int i, len, first_frame = 1, offset = 0, must_buffer = 0, hundred_percent;
    SANE_Parameters parm;
    SANE_Status status = SANE_STATUS_GOOD;   // Initialize status
    Image image = { nullptr, 0, 0, 0, 0, 0 };   // Use class's Image struct

    SANE_Word total_bytes = 0;   // Use SANE_Word

    // PNG related variables
    png_structp png_ptr = nullptr;
    png_infop info_ptr = nullptr;
    png_bytep pngbuf = nullptr;   // Buffer for one row of PNG data
    int pngrow = 0;   // Current position within pngbuf

    SANE_Byte *buffer;   // Buffer for sane_read
    size_t buffer_size = (128 * 1024);   // Increased buffer size (adjust as needed)
    buffer = (SANE_Byte *)malloc(buffer_size);
    if (!buffer) {
        qWarning() << "Failed to allocate read buffer.";
        return SANE_STATUS_NO_MEM;
    }

    do {
        if (!first_frame) {
#    ifdef SANE_STATUS_WARMING_UP
            do {
                status = sane_start(m_device);   // Use member variable m_device
                if (status == SANE_STATUS_WARMING_UP) {
                    qDebug() << "Scanner warming up (multi-frame)...";
                    QThread::msleep(500);
                }
            } while (status == SANE_STATUS_WARMING_UP);
#    else
            status = sane_start(m_device);
#    endif
            if (status != SANE_STATUS_GOOD) {
                qWarning() << "sane_start (multi-frame) failed:" << sane_strstatus(status);
                goto cleanup;   // Use goto for cleanup as in original
            }
        }

        status = sane_get_parameters(m_device, &parm);
        if (status != SANE_STATUS_GOOD) {
            qWarning() << "sane_get_parameters failed:" << sane_strstatus(status);
            goto cleanup;
        }

        // --- Frame setup (largely same as original) ---
        if (first_frame) {
            qDebug() << "Scan parameters - Format:" << parm.format << "Depth:" << parm.depth
                     << "Lines:" << parm.lines << "Pixels/Line:" << parm.pixels_per_line
                     << "Bytes/Line:" << parm.bytes_per_line;

            // Determine if buffering is needed
            must_buffer = 0;   // Reset flag
            if (parm.format >= SANE_FRAME_RED && parm.format <= SANE_FRAME_BLUE) {
                must_buffer = 1;   // Multi-frame requires buffering
                offset = parm.format - SANE_FRAME_RED;
            } else if (parm.lines < 0) {
                must_buffer = 1;   // Unknown height requires buffering
                offset = 0;
            }

            if (!must_buffer) {
                // Write PNG header directly if height is known
                write_png_header(parm.format, parm.pixels_per_line, parm.lines, parm.depth, ofp, &png_ptr, &info_ptr);
                if (!png_ptr || !info_ptr) {
                    qWarning() << "Failed to write PNG header.";
                    status = SANE_STATUS_IO_ERROR;   // Indicate error
                    goto cleanup;
                }
                // Allocate buffer for one row
                pngbuf = (png_bytep)malloc(parm.bytes_per_line);
                if (!pngbuf) {
                    qWarning() << "Failed to allocate PNG row buffer.";
                    status = SANE_STATUS_NO_MEM;
                    goto cleanup;
                }
                pngrow = 0;   // Reset row buffer position
            } else {
                // --- Buffering Logic (Requires proper Image/advance) ---
                qDebug() << "Buffering scan data (multi-frame or unknown height).";
                image.width = parm.bytes_per_line;
                image.height = (parm.lines > 0) ? (parm.lines + 1) : 512;   // Initial guess or use parameter
                image.line_buffer_size = image.width * ((parm.format == SANE_FRAME_RGB && parm.depth > 8) ? 6 : 3);   // Rough estimate for RGB16/RGB8
                size_t initial_alloc = (size_t)image.height * image.line_buffer_size;

                qDebug() << "Allocating image buffer: width=" << image.width << "initial_height=" << image.height << "bytes=" << initial_alloc;
                image.data = (unsigned char *)malloc(initial_alloc);

                if (!image.data) {
                    qWarning() << "Failed to allocate image buffer.";
                    status = SANE_STATUS_NO_MEM;
                    goto cleanup;
                }
                // Initialize image state
                image.x = 0;   // Start at the beginning of the first line
                image.y = 0;   // Start at the first line
                offset = (parm.format >= SANE_FRAME_RED && parm.format <= SANE_FRAME_BLUE) ? (parm.format - SANE_FRAME_RED) : 0;
                // Reset total bytes for accurate progress if buffering
                total_bytes = 0;
                // --- End Buffering Setup ---
            }
        } else {   // Not first frame (implies must_buffer was true before)
            // This part assumes multi-frame (R, G, B) and image buffer exists
            assert(parm.format >= SANE_FRAME_RED && parm.format <= SANE_FRAME_BLUE);
            assert(image.data != nullptr);
            offset = parm.format - SANE_FRAME_RED;
            image.x = 0;   // Reset position for the new frame/plane
            image.y = 0;
        }

        // Calculate expected bytes for progress (rough estimate if lines < 0)
        SANE_Word expected_bytes = (parm.lines > 0)
                ? (SANE_Word)parm.bytes_per_line * parm.lines
                : 0;   // Cannot calculate if lines unknown

        if (must_buffer && parm.format >= SANE_FRAME_RED && parm.format <= SANE_FRAME_BLUE) {
            expected_bytes *= 3;   // Expect 3 frames for R,G,B
        }
        hundred_percent = (expected_bytes > 0) ? 100 : -1;   // Use -1 if percentage cannot be calculated

        // --- Read loop ---
        while (true) {
            qDebug() << "Attempting to read from scanner...";
            status = sane_read(m_device, buffer, buffer_size, &len);
            qDebug() << "sane_read status:" << sane_strstatus(status) << "bytes read:" << len;

            if (status != SANE_STATUS_GOOD) {
                if (status == SANE_STATUS_EOF) {
                    qDebug() << "SANE_EOF received - scan complete";
                    break;
                } else {
                    qWarning() << "sane_read failed:" << sane_strstatus(status);
                    goto cleanup;
                }
            }

            if (len > 0) {
                // 创建一个图像行并发送信号
                QImage line;
                if (parm.format == SANE_FRAME_RGB) {
                    line = QImage(reinterpret_cast<uchar *>(buffer),
                                  parm.pixels_per_line,
                                  1,
                                  parm.bytes_per_line,
                                  QImage::Format_RGB888);
                } else {
                    line = QImage(reinterpret_cast<uchar *>(buffer),
                                  parm.pixels_per_line,
                                  1,
                                  parm.bytes_per_line,
                                  QImage::Format_Grayscale8);
                }

                if (!line.isNull()) {
                    emit previewLineAvailable(line.copy());
                    qDebug() << "Emitted preview line:" << line.width() << "x" << line.height();
                } else {
                    qWarning() << "Failed to create preview line image";
                }
            }

            total_bytes += (SANE_Word)len;

            // --- Emit Progress ---
            if (hundred_percent > 0) {
                double progr = ((double)total_bytes * 100.0) / (double)expected_bytes;
                if (progr > 100.0) progr = 100.0;
                emit scanProgress(static_cast<int>(progr));
            } else {
                // Maybe emit based on total bytes if percentage unknown?
                // emit scanProgress(-1); // Indicate unknown progress
            }

            if (must_buffer) {
                // --- Copy data to image buffer (Requires working advance) ---
                // This section needs careful review based on your 'advance' implementation
                // and how image.data should be structured (planar vs interleaved for RGB)
                int bytes_per_pixel = (parm.depth > 8) ? 2 : 1;
                int components = (parm.format == SANE_FRAME_RGB) ? 3 : 1;
                size_t required_size = (size_t)(image.y + 1) * image.width * bytes_per_pixel * components;   // Check calculation

                // Check if reallocation is needed (simplified)
                if (image.data && required_size > (size_t)image.height * image.line_buffer_size) {
                    // --- Reallocation logic needed here ---
                    qWarning() << "Image buffer reallocation needed but not implemented!";
                    // status = SANE_STATUS_NO_MEM; goto cleanup;
                }

                if (!image.data) {   // Check if buffer is valid
                    qWarning() << "Image buffer is null during read.";
                    status = SANE_STATUS_IO_ERROR;
                    goto cleanup;
                }

                unsigned char *current_pos = image.data + (image.y * image.width * components * bytes_per_pixel) + (image.x * components * bytes_per_pixel);
                // This assumes interleaved data storage. Planar for R/G/B needs different logic.

                if (parm.format == SANE_FRAME_GRAY || parm.format == SANE_FRAME_RGB) {
                    // Copy directly if gray or interleaved RGB
                    for (i = 0; i < len; ++i) {
                        // Ensure image.data is valid and large enough before writing
                        size_t required_offset = offset + 3 * i;
                        // Add proper size check based on image dimensions and depth
                        // if (required_offset >= allocated_buffer_size) { /* handle error/realloc */ }

                        image.data[required_offset] = buffer[i];

                        // Now advance returns bool, check for failure
                        if (!advance(&image))   // Call advance AFTER accessing the current pixel
                        {
                            qWarning() << "Advance failed, possibly out of memory.";
                            status = SANE_STATUS_NO_MEM;
                            goto cleanup;
                        }
                    }
                    offset += 3 * len;   // Update offset after the loop
                    break;
                } else {   // Planar R, G, B - requires stride
                    int stride = 3 * bytes_per_pixel;   // Assuming interleaved final buffer
                    for (i = 0; i < len; ++i) {
                        size_t pixel_index = (image.y * image.width + image.x);
                        // Check bounds
                        if (pixel_index * stride + offset >= required_size) {
                            qWarning() << "Buffer overflow detected in planar copy.";
                            status = SANE_STATUS_IO_ERROR;
                            goto cleanup;
                        }
                        image.data[pixel_index * stride + offset] = buffer[i];   // Place R/G/B in correct plane/offset
                        image.x++;
                        if (image.x >= image.width) {
                            image.x = 0;
                            image.y++;
                            // Potentially check/realloc height here
                        }
                    }
                }
                // --- End Buffering Logic Update ---

            } else {   // --- Write directly to PNG file ---
                int current_byte = 0;
                int bytes_remaining_in_buffer = len;

                while (bytes_remaining_in_buffer > 0) {
                    int bytes_to_copy = qMin(bytes_remaining_in_buffer, parm.bytes_per_line - pngrow);
                    memcpy(pngbuf + pngrow, buffer + current_byte, bytes_to_copy);

                    pngrow += bytes_to_copy;
                    current_byte += bytes_to_copy;
                    bytes_remaining_in_buffer -= bytes_to_copy;

                    // If a full row is ready, write it
                    if (pngrow == parm.bytes_per_line) {
                        // Handle 1-bit depth inversion if needed (as in original)
                        if (parm.depth == 1) {
                            for (int j = 0; j < parm.bytes_per_line; j++)
                                pngbuf[j] = ~pngbuf[j];
                        }
                        // Check if png_ptr is valid before writing
                        if (png_ptr && info_ptr) {
                            png_write_row(png_ptr, pngbuf);
                        } else {
                            qWarning() << "png_ptr or info_ptr is null, cannot write row.";
                            status = SANE_STATUS_IO_ERROR;   // Or another appropriate error
                            goto cleanup;
                        }
                        pngrow = 0;   // Reset row buffer position
                    }
                }
                // --- End Direct PNG Write ---
            }
        }   // End while(true) read loop

        first_frame = 0;   // Mark subsequent loops as not the first frame
    } while (!parm.last_frame);

    // --- Finalize based on buffering ---
    if (must_buffer) {
        // --- Write buffered image to PNG ---
        qDebug() << "Writing buffered image data to PNG.";
        if (image.data) {
            // Update final image height based on how far 'y' advanced
            image.height = image.y + (image.x > 0 ? 1 : 0);   // Include last partial line if any
            qDebug() << "Final buffered image height:" << image.height;

            // Write the PNG header now that dimensions are known
            write_png_header(parm.format, parm.pixels_per_line, image.height, parm.depth, ofp, &png_ptr, &info_ptr);
            if (!png_ptr || !info_ptr) {
                qWarning() << "Failed to write PNG header for buffered image.";
                status = SANE_STATUS_IO_ERROR;
                goto cleanup;
            }

            // Write the image data row by row
            int components = (parm.format == SANE_FRAME_RGB) ? 3 : 1;
            int bytes_per_pixel = (parm.depth > 8) ? 2 : 1;
            size_t row_stride = (size_t)image.width * components * bytes_per_pixel;   // Use calculated width

            for (int y = 0; y < image.height; ++y) {
                png_bytep row_ptr = image.data + y * row_stride;
                png_write_row(png_ptr, row_ptr);
            }

        } else {
            qWarning() << "Buffered image data is null, cannot write PNG.";
            status = SANE_STATUS_IO_ERROR;   // Or appropriate error
            // No need for goto cleanup here, will fall through to png_write_end check
        }
        // --- End Buffered Write ---
    } else {
        // If not buffering, check if there's a partial row left in pngbuf
        if (pngrow > 0) {
            qWarning() << "Warning: Scan ended with partial row data (" << pngrow << " bytes). This may indicate an incomplete scan or incorrect parameters.";
            // Optionally, pad the rest of the row with zeros/white and write it?
            // memset(pngbuf + pngrow, 0, parm.bytes_per_line - pngrow);
            // png_write_row(png_ptr, pngbuf);
        }
    }

    // Finalize PNG writing if png_ptr was initialized
    if (png_ptr && info_ptr) {
        png_write_end(png_ptr, info_ptr);
    }

    /* flush the output buffer */
    fflush(ofp);   // ofp should still be valid here

cleanup:
    // Store final status before cleanup might change it
    SANE_Status final_status = status;

    qDebug() << "Cleaning up scan resources. Final status:" << sane_strstatus(final_status);

    // Cleanup PNG resources
    if (png_ptr || info_ptr) {   // Check before destroying
        png_destroy_write_struct(&png_ptr, &info_ptr);   // Safe to call with null pointers too
    }
    free(pngbuf);   // Safe to call free(NULL)

    // Cleanup image buffer if allocated
    if (image.data) {
        free(image.data);
        image.data = nullptr;   // Avoid double free
    }

    // Cleanup sane_read buffer
    free(buffer);

    // Decide whether to cancel/close based on status
    // If EOF was reached, the scan completed naturally.
    // If another error occurred, maybe cancel? SANE docs are best here.
    // sane_cancel(m_device); // Might already be implicitly cancelled by error

    // Do NOT call sane_close or sane_exit here. Let ScannerDevice manage that.

    return final_status;   // Return the status captured before cleanup
}
#endif   // !_WIN32

// 添加一个新方法来生成测试图像
void ScannerDevice::generateTestImage(const QString &outputPath)
{
    qDebug() << "Generating test image at:" << outputPath;

    // 创建一个测试图像
    QImage testImage(800, 600, QImage::Format_RGB888);
    testImage.fill(Qt::white);

    // 绘制一些内容
    QPainter painter(&testImage);
    painter.setPen(QPen(Qt::black, 2));
    painter.drawRect(10, 10, testImage.width() - 20, testImage.height() - 20);

    // 添加文本
    QFont font = painter.font();
    font.setPointSize(24);
    painter.setFont(font);
    painter.drawText(testImage.rect(), Qt::AlignCenter, "测试扫描图像");

    // 添加当前日期时间
    font.setPointSize(12);
    painter.setFont(font);
    QString dateTime = QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss");
    painter.drawText(QRect(10, testImage.height() - 40, testImage.width() - 20, 30),
                     Qt::AlignRight, dateTime);

    // 添加一些几何图形
    painter.setBrush(QBrush(Qt::red));
    painter.drawEllipse(QPoint(100, 100), 50, 50);

    painter.setBrush(QBrush(Qt::blue));
    painter.drawRect(500, 100, 100, 100);

    painter.setBrush(QBrush(Qt::green));
    painter.drawRoundedRect(300, 300, 200, 100, 20, 20);

    // 保存图像
    if (testImage.save(outputPath)) {
        qDebug() << "Test image saved successfully";

        // 模拟一些延迟，模拟实际扫描过程
        for (int i = 0; i < 10; i++) {
            QThread::msleep(200);   // 200毫秒延迟
            emit scanProgress(i * 10);   // 0% - 90%

            // 发送预览行信号
            QImage line = testImage.copy(0, i * 60, testImage.width(), 1);
            emit previewLineAvailable(line);
        }

        emit scanProgress(100);   // 100%
        emit scanCompleted(testImage);
    } else {
        emit scanError(tr("无法保存测试图像"));
    }
}
