# Computer Graphics 2026 - Course Portfolio

**Student Information**
* **Name:** [MOHAMAD AYYAT]
* **Student ID:** [206621419]

## Academic Integrity & Workflow Agreement

By submitting work in this repository, I acknowledge and agree to the following:

* **Originality:** I certify that the work, code, and reports submitted are my own. While I may use AI assistance or external resources as permitted by the course guidelines, I am fully responsible for understanding, explaining, and defending every line of code and every claim in my reports.
* **Commit Frequency:** I understand that an iterative process is essential to this course. I am required to **commit my progress frequently**. A continuous "paper trail" of work is necessary for the teaching staff to verify the development process.
* **Penalty:** I acknowledge that submitting large amounts of work or entire projects in a single commit (or very few commits) **will be penalized**.
* **Responsibility:** I am the sole author of the content in this repository. Any plagiarism or failure to properly attribute external sources will be handled according to University of Haifa academic integrity policies.
* **Git Proficiency & Repository Hygiene:** I understand that managing a clean repository is a core part of the course. I will ensure that **large binary files, build artifacts, and temporary local files** are never committed (utilize the `.gitignore`). I am responsible for maintaining a readable history, using `git commit --amend` or `squash` where appropriate to keep the log professional and concise.
* **Visual Standards:** I acknowledge that **photos of a computer screen taken with a phone camera are strictly prohibited.** All visual results must be submitted as high-quality, direct screenshots, videos or renders, and integrated into the reports as detailed below. Similarly, images of text (such as screenshots of code or console output) are not allowed; all text must be provided as proper Markdown text for searchability and clarity.
* **The Report is Paramount:** I understand that while the code is the foundation, **the written report is the primary artifact being evaluated.** It must be clear, concise, and professional. A functioning project with a poor or disorganized report will not receive full credit.

---

## Getting New Assignments

New assignments and updates to the framework will be pushed to the template repository. To fetch the latest materials, ensure you have the template set as an `upstream` remote and pull regularly:
```bash
# Add the template as a remote (only needs to be done once)
git remote add upstream https://github.com/xCraftCourses/cgatuoh26-cg-at-uoh-26-cgvibes-uoh-template

# Pull the latest changes
git pull upstream main
```

---

## Assignment Workflow & Reports

Each assignment consists of multiple technical tasks. Your submission must reflect an incremental development process.

### The Development Cycle
For every task within an assignment, you should follow this sequence:
1.  **Develop:** Complete the code for the specific task.
2.  **Document:** Update the corresponding assignment report (`.md` format) with a new section dedicated to that task.
3.  **Commit:** Perform a Git commit and push. 

**Requirements:**
*   **Minimum Commit Rule:** There must be **at least one commit per task**. You are encouraged to commit more frequently, and you may update your code after a task is "finished," but the paper trail must show the completion of individual tasks.
*   **Report Structure:** Each task must have its own clear section in your report detailing your approach and results.
*   **TOC Maintenance:** While reports can be stored in their respective project folders, you **must** update the Table of Contents in this `README.md` whenever a new report or project is added.

---

## Media & Documentation Guidelines

To keep the repository clean and the reports professional, follow these standards for including media:

* **Images (Screenshots/Renders):** 
    * Store all images in an `assets/` folder within the specific project directory.
    * Use relative paths in your Markdown: `![Description](./assets/image.png)`.
    * Use PNG for UI/Screenshots and JPG for high-detail renders to manage file size.
* **Videos & Demos:**
    * **Short Clips:** For animations under 5 seconds, use a GIF if the file size remains under 3MB.
    * **Longer Demos:** Do **not** commit large video files (MP4/MOV) directly to the repository. Instead, drag and drop the video into the GitHub web editor to host it on GitHub's servers, or upload to YouTube/Vimeo and provide a link with a thumbnail.
* **No "Heavy" Assets:** Never commit raw video footage, massive 4K textures, or uncompressed high-poly meshes unless specifically instructed. Use the `.gitignore` to keep these out of your history.

---

## Table of Contents
| Assignment | Link |
| :--- | :--- |
| **Project 1: My First Project** | [View Folder](./MyFirstProject/) |

---
*Note: Please ensure all internal links remain functional as you add new folders.*
