# bootloader_gui.py
import sys
import os
import time
import struct
import serial
import serial.tools.list_ports
from PyQt5.QtWidgets import (QApplication, QMainWindow, QVBoxLayout, QHBoxLayout, 
                           QGridLayout, QWidget, QPushButton, QLabel, QComboBox, 
                           QTextEdit, QProgressBar, QFileDialog, QMessageBox, 
                           QGroupBox, QLineEdit, QSpinBox, QCheckBox, QTabWidget)
from PyQt5.QtCore import QThread, pyqtSignal, QTimer
from PyQt5.QtGui import QFont, QPixmap, QIcon, QTextCursor
import threading
from datetime import datetime

# Bootloader komutları
CMD_GET_INFO = 0x10
CMD_ERASE_FLASH = 0x11
CMD_WRITE_FLASH = 0x12
CMD_READ_FLASH = 0x13
CMD_GET_CHECKSUM = 0x14
CMD_JUMP_TO_APP = 0x15

# Yanıt kodları
RESP_OK = 0x90
RESP_ERROR = 0x91
RESP_INVALID_CMD = 0x92

class SerialWorker(QThread):
    """UART işlemleri için worker thread"""
    progress_update = pyqtSignal(int)
    status_update = pyqtSignal(str)
    uart_tx = pyqtSignal(bytes)  # UART TX data signal
    uart_rx = pyqtSignal(bytes)  # UART RX data signal
    finished = pyqtSignal(bool)
    
    def __init__(self, serial_port, operation, **kwargs):
        super().__init__()
        self.serial_port = serial_port
        self.operation = operation
        self.kwargs = kwargs
        
    def run(self):
        try:
            if self.operation == "get_info":
                self.get_bootloader_info()
            elif self.operation == "flash_firmware":
                self.flash_firmware()
            elif self.operation == "jump_to_app":
                self.jump_to_application()
            elif self.operation == "read_flash":
                self.read_flash()
            elif self.operation == "erase_flash":
                self.erase_flash()
        except Exception as e:
            self.status_update.emit(f"Hata: {str(e)}")
            self.finished.emit(False)
    
    def flush_buffers(self):
        """UART buffer'larını temizle"""
        try:
            # Input buffer'ı temizle (alınan ama okunmamış veriler)
            self.serial_port.reset_input_buffer()
            
            # Output buffer'ı temizle (gönderilmeyi bekleyen veriler)
            self.serial_port.reset_output_buffer()
            
            # Ek güvenlik: Kalan verileri oku ve at
            self.serial_port.timeout = 0.1  # Kısa timeout
            leftover = self.serial_port.read_all()
            if leftover:
                self.status_update.emit(f"Buffer temizlendi: {len(leftover)} byte atıldı")
                
        except Exception as e:
            self.status_update.emit(f"Buffer temizleme hatası: {str(e)}")
    
    def write_with_debug(self, data):
        """UART'a veri yaz ve debug sinyali gönder"""
        # Buffer'ları temizle
        self.flush_buffers()
        
        self.uart_tx.emit(data)
        self.serial_port.write(data)
        
        # Yazma işleminin tamamlanmasını bekle
        self.serial_port.flush()
        
    def read_with_debug(self, size, timeout=1.0):
        """UART'tan veri oku ve debug sinyali gönder"""
        old_timeout = self.serial_port.timeout
        self.serial_port.timeout = timeout
        
        data = self.serial_port.read(size)
        self.serial_port.timeout = old_timeout
        
        if data:
            self.uart_rx.emit(data)
        return data
    
    def get_bootloader_info(self):
        try:
            # İşlem öncesi buffer temizle
            self.flush_buffers()
            
            # GET_INFO komutu gönder
            cmd = bytes([CMD_GET_INFO])
            self.write_with_debug(cmd)
            time.sleep(0.1)
            
            response = self.read_with_debug(6)
            if len(response) >= 6 and response[0] == RESP_OK:
                version = response[1]
                app_addr = struct.unpack('<I', response[2:6])[0]
                self.status_update.emit(f"Bootloader Sürümü: {version}")
                self.status_update.emit(f"Uygulama Adresi: 0x{app_addr:08X}")
                self.finished.emit(True)
            else:
                self.status_update.emit(f"Bootloader bilgisi alınamadı! Yanıt: {response.hex() if response else 'YOK'}")
                self.finished.emit(False)
        except Exception as e:
            self.status_update.emit(f"Bootloader info hatası: {str(e)}")
            self.finished.emit(False)
    
    def flash_firmware(self):
        try:
            file_path = self.kwargs['file_path']
            start_address = self.kwargs['start_address']
            
            # İşlem öncesi buffer temizle
            self.flush_buffers()
            
            self.status_update.emit(f"Firmware yükleniyor: {os.path.basename(file_path)}")
            
            # Bin dosyasını oku
            with open(file_path, 'rb') as f:
                firmware_data = f.read()
            
            self.status_update.emit(f"Dosya boyutu: {len(firmware_data)} bytes")
            
            # Önce flash'ı sil - HEADER'I AYRI GÖNDER
            self.status_update.emit("Flash siliniyor...")
            
            # Komut gönder
            cmd_byte = bytes([CMD_ERASE_FLASH])
            self.write_with_debug(cmd_byte)
            time.sleep(0.1)  # Komut işlenmesi için bekle
            
            # Address gönder
            addr_bytes = struct.pack('<I', start_address)
            self.write_with_debug(addr_bytes)
            time.sleep(0.1)
            
            # Size gönder
            size_bytes = struct.pack('<I', len(firmware_data))
            self.write_with_debug(size_bytes)
            time.sleep(2.0)
            
            response = self.read_with_debug(1, 5.0)
            if len(response) == 0 or response[0] != RESP_OK:
                self.status_update.emit(f"Flash silme hatası! Yanıt: {response.hex() if response else 'YOK'}")
                self.finished.emit(False)
                return
            
            self.status_update.emit("Flash silindi, yazma başlıyor...")
            
            # Chunk boyutunu küçült
            chunk_size = 128
            total_chunks = (len(firmware_data) + chunk_size - 1) // chunk_size
            
            for i in range(total_chunks):
                start_idx = i * chunk_size
                end_idx = min(start_idx + chunk_size, len(firmware_data))
                chunk = firmware_data[start_idx:end_idx]
                
                # Her chunk öncesi buffer temizle
                if i % 5 == 0:  # Daha sık temizle
                    self.flush_buffers()
                
                write_address = start_address + start_idx
                
                # WRITE KOMUTUNU AYRI AYRI GÖNDER
                # 1. Komut gönder
                cmd_byte = bytes([CMD_WRITE_FLASH])
                self.write_with_debug(cmd_byte)
                time.sleep(0.05)
                
                # 2. Address gönder
                addr_bytes = struct.pack('<I', write_address)
                self.write_with_debug(addr_bytes)
                time.sleep(0.05)
                
                # 3. Size gönder
                size_bytes = struct.pack('<I', len(chunk))
                self.write_with_debug(size_bytes)
                time.sleep(0.05)
                
                # 4. Data gönder
                self.write_with_debug(chunk)
                
                # Debug: Detaylı bilgi
                if i % 10 == 0:
                    self.status_update.emit(f"Chunk {i+1}/{total_chunks}: Addr=0x{write_address:08X}, Size={len(chunk)}")
            
                # Yanıt bekle
                response = self.read_with_debug(1, 5.0)  # Timeout artır
                if len(response) == 0 or response[0] != RESP_OK:
                    self.status_update.emit(f"Yazma hatası chunk {i+1}/{total_chunks}, yanıt: {response.hex() if response else 'YOK'}")
                    self.finished.emit(False)
                    return
                
                # Progress güncelle
                progress = int((i + 1) * 100 / total_chunks)
                self.progress_update.emit(progress)
                
                if i % 10 == 0:
                    self.status_update.emit(f"✅ İlerleme: {i+1}/{total_chunks} chunk (%{progress})")
                
                time.sleep(0.1)  # Chunk'lar arası daha fazla bekle

            self.status_update.emit("Firmware başarıyla yüklendi!")
            self.finished.emit(True)
            
        except Exception as e:
            self.status_update.emit(f"Flash hatası: {str(e)}")
            self.finished.emit(False)
    
    def jump_to_application(self):
        try:
            # İşlem öncesi buffer temizle
            self.flush_buffers()
            
            self.status_update.emit("Uygulamaya atlıyor...")
            cmd = bytes([CMD_JUMP_TO_APP])
            self.write_with_debug(cmd)
            time.sleep(0.1)
            
            response = self.read_with_debug(1, 2.0)
            if len(response) > 0 and response[0] == RESP_OK:
                self.status_update.emit("Uygulama başlatıldı!")
                self.finished.emit(True)
            else:
                self.status_update.emit(f"Uygulama başlatma hatası! Yanıt: {response.hex() if response else 'YOK'}")
                self.finished.emit(False)
        except Exception as e:
            self.status_update.emit(f"Jump hatası: {str(e)}")
            self.finished.emit(False)
    
    def read_flash(self):
        try:
            # İşlem öncesi buffer temizle
            self.flush_buffers()
            
            address = self.kwargs['address']
            size = self.kwargs['size']
            
            read_cmd = struct.pack('<BII', CMD_READ_FLASH, address, size)
            self.write_with_debug(read_cmd)
            time.sleep(0.1)
            
            response = self.read_with_debug(1 + size, 3.0)
            if len(response) > 0 and response[0] == RESP_OK:
                data = response[1:]
                hex_str = ' '.join(f'{b:02X}' for b in data)
                self.status_update.emit(f"Flash Okudu (0x{address:08X}): {hex_str}")
                self.finished.emit(True)
            else:
                self.status_update.emit(f"Flash okuma hatası! Yanıt: {response.hex() if response else 'YOK'}")
                self.finished.emit(False)
        except Exception as e:
            self.status_update.emit(f"Read hatası: {str(e)}")
            self.finished.emit(False)
    
    def erase_flash(self):
        try:
            # İşlem öncesi buffer temizle
            self.flush_buffers()
            
            address = self.kwargs['address']
            size = self.kwargs['size']
            
            erase_cmd = struct.pack('<BII', CMD_ERASE_FLASH, address, size)
            self.write_with_debug(erase_cmd)
            time.sleep(2.0)
            
            response = self.read_with_debug(1, 5.0)
            if len(response) > 0 and response[0] == RESP_OK:
                self.status_update.emit(f"Flash silindi (0x{address:08X}, {size} bytes)")
                self.finished.emit(True)
            else:
                self.status_update.emit(f"Flash silme hatası! Yanıt: {response.hex() if response else 'YOK'}")
                self.finished.emit(False)
        except Exception as e:
            self.status_update.emit(f"Erase hatası: {str(e)}")
            self.finished.emit(False)

class BootloaderGUI(QMainWindow):
    def __init__(self):
        super().__init__()
        self.serial_port = None
        self.worker = None
        self.uart_debug_enabled = True
        self.init_ui()
        self.refresh_ports()
        
    def init_ui(self):
        self.setWindowTitle("STM32F446 Bootloader GUI v1.0 - Debug Mode")
        self.setGeometry(100, 100, 1000, 800)
        
        # Ana widget
        central_widget = QWidget()
        self.setCentralWidget(central_widget)
        
        # Ana layout
        main_layout = QVBoxLayout(central_widget)
        
        # Üst panel - Seri port bağlantısı
        self.create_connection_group(main_layout)
        
        # Orta panel - Firmware yükleme
        self.create_firmware_group(main_layout)
        
        # Alt panel - Manuel işlemler
        self.create_manual_group(main_layout)
        
        # En alt - Tabbed log alanı
        self.create_tabbed_status_group(main_layout)
        
    def create_connection_group(self, parent_layout):
        group = QGroupBox("Seri Port Bağlantısı")
        layout = QHBoxLayout(group)
        
        # Port seçimi
        self.port_combo = QComboBox()
        layout.addWidget(QLabel("Port:"))
        layout.addWidget(self.port_combo)
        
        # Baud rate
        self.baud_combo = QComboBox()
        self.baud_combo.addItems(["9600", "19200", "38400", "57600", "115200", "230400"])
        self.baud_combo.setCurrentText("115200")
        layout.addWidget(QLabel("Baud:"))
        layout.addWidget(self.baud_combo)
        
        # Debug checkbox
        self.debug_checkbox = QCheckBox("UART Debug")
        self.debug_checkbox.setChecked(True)
        self.debug_checkbox.stateChanged.connect(self.toggle_uart_debug)
        layout.addWidget(self.debug_checkbox)
        
        # Bağlantı butonları
        self.refresh_btn = QPushButton("Yenile")
        self.refresh_btn.clicked.connect(self.refresh_ports)
        layout.addWidget(self.refresh_btn)
        
        self.connect_btn = QPushButton("Bağlan")
        self.connect_btn.clicked.connect(self.toggle_connection)
        layout.addWidget(self.connect_btn)
        
        # Bağlantı durumu
        self.connection_status = QLabel("●")
        self.connection_status.setStyleSheet("color: red; font-size: 16px;")
        layout.addWidget(self.connection_status)
        
        parent_layout.addWidget(group)
        
    def create_firmware_group(self, parent_layout):
        group = QGroupBox("Firmware Yükleme")
        layout = QVBoxLayout(group)
        
        # Dosya seçimi
        file_layout = QHBoxLayout()
        self.file_path_edit = QLineEdit()
        self.file_path_edit.setPlaceholderText("Firmware dosyası seçin (.bin)")
        file_layout.addWidget(self.file_path_edit)
        
        self.browse_btn = QPushButton("Gözat...")
        self.browse_btn.clicked.connect(self.browse_firmware)
        file_layout.addWidget(self.browse_btn)
        
        layout.addLayout(file_layout)
        
        # Adres ayarları
        addr_layout = QHBoxLayout()
        addr_layout.addWidget(QLabel("Başlangıç Adresi:"))
        self.start_addr_edit = QLineEdit("0x08008000")
        addr_layout.addWidget(self.start_addr_edit)
        addr_layout.addStretch()
        
        layout.addLayout(addr_layout)
        
        # İşlem butonları
        btn_layout = QHBoxLayout()
        
        self.flash_btn = QPushButton("Firmware Yükle")
        self.flash_btn.clicked.connect(self.flash_firmware)
        self.flash_btn.setEnabled(False)
        btn_layout.addWidget(self.flash_btn)
        
        self.jump_btn = QPushButton("Uygulamayı Çalıştır")
        self.jump_btn.clicked.connect(self.jump_to_app)
        self.jump_btn.setEnabled(False)
        btn_layout.addWidget(self.jump_btn)
        
        layout.addLayout(btn_layout)
        
        parent_layout.addWidget(group)
        
    def create_manual_group(self, parent_layout):
        group = QGroupBox("Manuel İşlemler")
        layout = QGridLayout(group)
        
        # Bootloader bilgisi
        self.info_btn = QPushButton("Bootloader Bilgisi")
        self.info_btn.clicked.connect(self.get_bootloader_info)
        self.info_btn.setEnabled(False)
        layout.addWidget(self.info_btn, 0, 0)
        
        # Flash okuma
        layout.addWidget(QLabel("Flash Oku:"), 1, 0)
        self.read_addr_edit = QLineEdit("0x08008000")
        layout.addWidget(self.read_addr_edit, 1, 1)
        self.read_size_spin = QSpinBox()
        self.read_size_spin.setRange(1, 256)
        self.read_size_spin.setValue(16)
        layout.addWidget(self.read_size_spin, 1, 2)
        
        self.read_btn = QPushButton("Oku")
        self.read_btn.clicked.connect(self.read_flash)
        self.read_btn.setEnabled(False)
        layout.addWidget(self.read_btn, 1, 3)
        
        # Flash silme
        layout.addWidget(QLabel("Flash Sil:"), 2, 0)
        self.erase_addr_edit = QLineEdit("0x08008000")
        layout.addWidget(self.erase_addr_edit, 2, 1)
        self.erase_size_edit = QLineEdit("32768")
        layout.addWidget(self.erase_size_edit, 2, 2)
        
        self.erase_btn = QPushButton("Sil")
        self.erase_btn.clicked.connect(self.erase_flash)
        self.erase_btn.setEnabled(False)
        layout.addWidget(self.erase_btn, 2, 3)
        
        parent_layout.addWidget(group)
        
    def create_tabbed_status_group(self, parent_layout):
        group = QGroupBox("Durum ve Debug")
        layout = QVBoxLayout(group)
        
        # Progress bar
        self.progress_bar = QProgressBar()
        layout.addWidget(self.progress_bar)
        
        # Tab widget oluştur
        self.tab_widget = QTabWidget()
        
        # Ana log tab'ı
        self.log_text = QTextEdit()
        self.log_text.setMaximumHeight(200)
        self.log_text.setFont(QFont("Consolas", 9))
        self.tab_widget.addTab(self.log_text, "Ana Log")
        
        # UART Debug tab'ı
        self.uart_debug_text = QTextEdit()
        self.uart_debug_text.setMaximumHeight(200)
        self.uart_debug_text.setFont(QFont("Consolas", 8))
        self.uart_debug_text.setStyleSheet("background-color: #1e1e1e; color: #ffffff;")
        self.tab_widget.addTab(self.uart_debug_text, "UART Debug")
        
        layout.addWidget(self.tab_widget)
        
        # Alt butonlar
        btn_layout = QHBoxLayout()
        
        self.clear_log_btn = QPushButton("Ana Logu Temizle")
        self.clear_log_btn.clicked.connect(self.clear_log)
        btn_layout.addWidget(self.clear_log_btn)
        
        self.clear_uart_btn = QPushButton("UART Logu Temizle")
        self.clear_uart_btn.clicked.connect(self.clear_uart_log)
        btn_layout.addWidget(self.clear_uart_btn)
        
        btn_layout.addStretch()
        
        layout.addLayout(btn_layout)
        
        parent_layout.addWidget(group)
        
    def toggle_uart_debug(self, state):
        """UART debug'ı aç/kapat"""
        self.uart_debug_enabled = state == 2  # Qt.Checked
        if self.uart_debug_enabled:
            self.uart_log("UART Debug aktif")
        else:
            self.uart_log("UART Debug pasif")
    
    def uart_log_tx(self, data):
        """UART TX verisini logla"""
        if not self.uart_debug_enabled:
            return
            
        timestamp = datetime.now().strftime("%H:%M:%S.%f")[:-3]  # Son 3 karakter (mikrosaniye)
        hex_str = ' '.join(f'{b:02X}' for b in data)
        ascii_str = ''.join(chr(b) if 32 <= b <= 126 else '.' for b in data)
        
        message = f"[{timestamp}] TX ({len(data):3d}): {hex_str:<48} | {ascii_str}"
        self.uart_log(message, color="#ff6b6b")  # Kırmızı - TX
    
    def uart_log_rx(self, data):
        """UART RX verisini logla"""
        if not self.uart_debug_enabled:
            return
            
        timestamp = datetime.now().strftime("%H:%M:%S.%f")[:-3]  # Son 3 karakter (mikrosaniye)
        hex_str = ' '.join(f'{b:02X}' for b in data)
        ascii_str = ''.join(chr(b) if 32 <= b <= 126 else '.' for b in data)
        
        message = f"[{timestamp}] RX ({len(data):3d}): {hex_str:<48} | {ascii_str}"
        self.uart_log(message, color="#4ecdc4")  # Turkuaz - RX
    
    def uart_log(self, message, color="#ffffff"):
        """UART debug mesajı ekle"""
        cursor = self.uart_debug_text.textCursor()
        cursor.movePosition(QTextCursor.End)
        
        # Renk uygula
        self.uart_debug_text.setTextColor(self.uart_debug_text.palette().color(self.uart_debug_text.palette().Text))
        if color != "#ffffff":
            from PyQt5.QtGui import QColor
            self.uart_debug_text.setTextColor(QColor(color))
        
        self.uart_debug_text.append(message)
        
        # Auto scroll
        scrollbar = self.uart_debug_text.verticalScrollBar()
        scrollbar.setValue(scrollbar.maximum())
    
    def refresh_ports(self):
        """Mevcut seri portları yenile"""
        self.port_combo.clear()
        ports = serial.tools.list_ports.comports()
        for port in ports:
            self.port_combo.addItem(f"{port.device} - {port.description}")
        
        if not ports:
            self.log("Hiç seri port bulunamadı!")
        else:
            self.log(f"{len(ports)} adet seri port bulundu.")
            
    def toggle_connection(self):
        """Seri port bağlantısını aç/kapat"""
        if self.serial_port and self.serial_port.is_open:
            self.disconnect_serial()
        else:
            self.connect_serial()
            
    def connect_serial(self):
        """Seri porta bağlan"""
        try:
            if self.port_combo.count() == 0:
                self.log("Hiç seri port bulunamadı!")
                return
            
            port_text = self.port_combo.currentText()
            port_name = port_text.split(" - ")[0]
            baud_rate = int(self.baud_combo.currentText())
            
            self.serial_port = serial.Serial(
                port=port_name,
                baudrate=baud_rate,
                bytesize=serial.EIGHTBITS,
                parity=serial.PARITY_NONE,
                stopbits=serial.STOPBITS_ONE,
                timeout=1.0
            )
            
            if self.serial_port.is_open:
                self.log(f"Bağlantı kuruldu: {port_name} @ {baud_rate}")
                self.uart_log(f"=== UART Bağlantısı Kuruldu: {port_name} @ {baud_rate} ===")
                self.connection_status.setStyleSheet("color: green; font-size: 16px;")
                self.connect_btn.setText("Bağlantıyı Kes")
                self.enable_controls(True)
            else:
                self.log("Bağlantı kurulamadı!")
                
        except Exception as e:
            self.log(f"Bağlantı hatası: {str(e)}")
            
    def disconnect_serial(self):
        """Seri port bağlantısını kes"""
        try:
            if self.serial_port and self.serial_port.is_open:
                self.serial_port.close()
                
            self.log("Bağlantı kesildi.")
            self.uart_log("=== UART Bağlantısı Kesildi ===")
            self.connection_status.setStyleSheet("color: red; font-size: 16px;")
            self.connect_btn.setText("Bağlan")
            self.enable_controls(False)
            
        except Exception as e:
            self.log(f"Bağlantı kesme hatası: {str(e)}")
            
    def enable_controls(self, enabled):
        """Kontrolleri aktif/pasif et"""
        self.flash_btn.setEnabled(enabled)
        self.jump_btn.setEnabled(enabled)
        self.info_btn.setEnabled(enabled)
        self.read_btn.setEnabled(enabled)
        self.erase_btn.setEnabled(enabled)
        
    def browse_firmware(self):
        """Firmware dosyası seç"""
        file_path, _ = QFileDialog.getOpenFileName(
            self, 
            "Firmware Dosyası Seç", 
            "", 
            "Binary Files (*.bin);;All Files (*)"
        )
        
        if file_path:
            self.file_path_edit.setText(file_path)
            file_size = os.path.getsize(file_path)
            self.log(f"Dosya seçildi: {os.path.basename(file_path)} ({file_size} bytes)")
            
    def flash_firmware(self):
        """Firmware yükle"""
        if not self.serial_port or not self.serial_port.is_open:
            self.log("Seri port bağlantısı yok!")
            return
            
        file_path = self.file_path_edit.text()
        if not file_path or not os.path.exists(file_path):
            self.log("Geçerli bir firmware dosyası seçin!")
            return
            
        try:
            start_addr_text = self.start_addr_edit.text()
            if start_addr_text.startswith("0x"):
                start_address = int(start_addr_text, 16)
            else:
                start_address = int(start_addr_text)
                
        except ValueError:
            self.log("Geçersiz başlangıç adresi!")
            return
            
        # Onay iste
        reply = QMessageBox.question(
            self, 
            "Firmware Yükleme", 
            f"Firmware yüklenecek:\n\nDosya: {os.path.basename(file_path)}\nAdres: 0x{start_address:08X}\n\nDevam etmek istiyor musunuz?",
            QMessageBox.Yes | QMessageBox.No
        )
        
        if reply == QMessageBox.Yes:
            self.start_worker("flash_firmware", file_path=file_path, start_address=start_address)
            
    def jump_to_app(self):
        """Uygulamaya atla"""
        if not self.serial_port or not self.serial_port.is_open:
            self.log("Seri port bağlantısı yok!")
            return
            
        self.start_worker("jump_to_app")
        
    def get_bootloader_info(self):
        """Bootloader bilgisini al"""
        if not self.serial_port or not self.serial_port.is_open:
            self.log("Seri port bağlantısı yok!")
            return
            
        self.start_worker("get_info")
        
    def read_flash(self):
        """Flash oku"""
        if not self.serial_port or not self.serial_port.is_open:
            self.log("Seri port bağlantısı yok!")
            return
            
        try:
            addr_text = self.read_addr_edit.text()
            if addr_text.startswith("0x"):
                address = int(addr_text, 16)
            else:
                address = int(addr_text)
                
            size = self.read_size_spin.value()
            
        except ValueError:
            self.log("Geçersiz adres!")
            return
            
        self.start_worker("read_flash", address=address, size=size)
        
    def erase_flash(self):
        """Flash sil"""
        if not self.serial_port or not self.serial_port.is_open:
            self.log("Seri port bağlantısı yok!")
            return
            
        try:
            addr_text = self.erase_addr_edit.text()
            if addr_text.startswith("0x"):
                address = int(addr_text, 16)
            else:
                address = int(addr_text)
                
            size = int(self.erase_size_edit.text())
            
        except ValueError:
            self.log("Geçersiz adres veya boyut!")
            return
            
        # Onay iste
        reply = QMessageBox.question(
            self, 
            "Flash Silme", 
            f"Flash silinecek:\n\nAdres: 0x{address:08X}\nBoyut: {size} bytes\n\nDevam etmek istiyor musunuz?",
            QMessageBox.Yes | QMessageBox.No
        )
        
        if reply == QMessageBox.Yes:
            self.start_worker("erase_flash", address=address, size=size)
            
    def start_worker(self, operation, **kwargs):
        """Worker thread başlat"""
        if self.worker and self.worker.isRunning():
            self.log("İşlem devam ediyor...")
            return
            
        self.worker = SerialWorker(self.serial_port, operation, **kwargs)
        self.worker.progress_update.connect(self.update_progress)
        self.worker.status_update.connect(self.log)
        self.worker.uart_tx.connect(self.uart_log_tx)
        self.worker.uart_rx.connect(self.uart_log_rx)
        self.worker.finished.connect(self.worker_finished)
        
        self.enable_controls(False)
        self.progress_bar.setValue(0)
        self.worker.start()
        
    def update_progress(self, value):
        """Progress bar güncelle"""
        self.progress_bar.setValue(value)
        
    def worker_finished(self, success):
        """Worker tamamlandı"""
        self.enable_controls(True)
        if success:
            self.progress_bar.setValue(100)
        else:
            self.progress_bar.setValue(0)
            
    def log(self, message):
        """Ana log mesajı ekle"""
        timestamp = time.strftime("%H:%M:%S")
        self.log_text.append(f"[{timestamp}] {message}")
        
    def clear_log(self):
        """Ana log temizle"""
        self.log_text.clear()
        
    def clear_uart_log(self):
        """UART debug log temizle"""
        self.uart_debug_text.clear()
        if self.uart_debug_enabled:
            self.uart_log("UART Debug log temizlendi")
        
    def closeEvent(self, event):
        """Uygulama kapatılırken"""
        if self.worker and self.worker.isRunning():
            self.worker.terminate()
            self.worker.wait()
            
        if self.serial_port and self.serial_port.is_open:
            self.serial_port.close()
            
        event.accept()

def main():
    app = QApplication(sys.argv)
    
    # Uygulama ikonu (opsiyonel)
    app.setWindowIcon(QIcon('icon.png'))  # Eğer icon.png dosyanız varsa
    
    window = BootloaderGUI()
    window.show()
    
    sys.exit(app.exec_())

if __name__ == "__main__":
    main()