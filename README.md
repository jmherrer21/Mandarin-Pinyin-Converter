# Mandarin Pinyin Converter

Program to add pinyin to PDF/TXT files containing Mandarin hanzi. Can export as PDF or use an HTML file which shows definitions and will read text using TTS. 

The program uses **libmupdf** for PDF extraction, **cppjieba + limonp** to segment words for accurate boundaries, **CEDICT** as a dictionary, **wkhtmltopdf** for PDF exporting, and **piper** for a Neural TTS audio output.

[[Demo Video]](MPC_Demo.mp4)

<img width="1278" height="698" alt="Screenshot 2026-05-18 082109" src="https://github.com/user-attachments/assets/c1fecf2e-6eb1-4e7f-b7a4-9710b4328988" />


<img width="1277" height="694" alt="Screenshot 2026-05-18 081632" src="https://github.com/user-attachments/assets/e75ad050-e48b-4e60-b72a-fa7baa89d1a8" />
