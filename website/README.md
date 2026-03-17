# RavBot Official Website

This directory contains the source code for the official RavBot website, built with VitePress.

## 🚀 Getting Started

### Prerequisites

- Node.js 16+ (recommended 18+)
- npm or yarn

### Local Development

1. **Install dependencies:**
   ```bash
   npm install
   ```

2. **Start development server:**
   ```bash
   npm run docs:dev
   ```

3. **Open in browser:**
   ```
   http://localhost:5173
   ```

The site will hot-reload as you make changes to the markdown files.

### Build for Production

```bash
npm run docs:build
```

The built website will be in `.vitepress/dist/`.

### Preview Built Site

```bash
npm run docs:preview
```

## 📁 Project Structure

```
website/
├── .vitepress/
│   ├── config.ts           # VitePress configuration
│   ├── theme/
│   │   ├── index.ts        # Theme entry point
│   │   └── custom.css      # Custom styles
│   └── dist/               # Built output
├── guide/                  # Documentation pages
│   ├── getting-started.md
│   ├── installation.md
│   ├── features.md
│   ├── architecture.md
│   ├── configuration.md
│   ├── plugins.md
│   ├── cli-reference.md
│   ├── building.md
│   ├── contributing.md
│   └── documentation.md
├── index.md                # Homepage
├── package.json            # Dependencies
└── README.md               # This file
```

## 🎨 Customization

### Theme Colors

Edit `.vitepress/theme/custom.css` to customize colors:

```css
:root {
  --vp-c-brand: #2d3748;
  --vp-c-accent: #06b6d4;
  /* ... more colors ... */
}
```

### Navigation

Edit `.vitepress/config.ts` to modify:
- Navigation bar links
- Sidebar structure
- Social links
- Footer content

### Content

Add or edit markdown files in the `guide/` directory. They will automatically appear in the sidebar based on the configuration.

## 📝 Adding New Pages

1. Create a new markdown file in `guide/`:
   ```bash
   touch guide/new-page.md
   ```

2. Add content using markdown:
   ```markdown
   # Page Title

   Page content here...
   ```

3. Update `.vitepress/config.ts` to add the page to navigation/sidebar

4. Changes are reflected immediately in development mode

## 🚢 Deployment

### GitHub Pages Automatic Deployment

The website automatically deploys to GitHub Pages when changes are pushed to:
- `main` branch (production)
- `develop` branch (staging)

The deployment is handled by `.github/workflows/deploy-website.yml`.

### Manual Deployment

If you need to manually deploy:

```bash
# Build the site
npm run docs:build

# The built files in .vitepress/dist/ can be deployed to any static host
```

### Custom Domain Setup

To use a custom domain (e.g., ravbot.io):

1. Add `CNAME` file to `.vitepress/dist/`:
   ```
   ravbot.io
   ```

2. Configure DNS records pointing to GitHub Pages

3. Enable custom domain in GitHub repository settings

## 📚 VitePress Documentation

For more information about VitePress features and configuration:
- [VitePress Docs](https://vitepress.dev/)
- [Markdown Guide](https://vitepress.dev/guide/markdown)
- [Theme Customization](https://vitepress.dev/guide/extending-default-theme)

## 🔍 Search

The website includes local search functionality powered by MiniSearch. This works client-side without requiring external services.

## 📊 Analytics (Optional)

To add analytics, uncomment and configure in `.vitepress/config.ts`:

```typescript
// Example: Add Google Analytics
head: [
  ['script', {
    async: '',
    src: 'https://www.googletagmanager.com/gtag/js?id=GA_ID'
  }]
]
```

## 🐛 Troubleshooting

### Build Errors

If you encounter build errors:

```bash
# Clear node_modules and reinstall
rm -rf node_modules package-lock.json
npm install

# Clear VitePress cache
rm -rf .vitepress/.temp

# Try building again
npm run docs:build
```

### Port Already in Use

If port 5173 is already in use:

```bash
npm run docs:dev -- --port 3000
```

### Slow Performance

Clear the cache and rebuild:

```bash
npm run docs:build
npm run docs:preview
```

## 📜 License

This website content is part of RavBot, released under the [MIT License](https://github.com/RavBot/ravbot/blob/main/LICENSE).

## 🤝 Contributing

Contributions to the documentation are welcome! Please:

1. Follow the existing markdown style
2. Keep pages focused and clear
3. Add examples where helpful
4. Update navigation if adding new pages
5. Submit a pull request

See [Contributing Guide](/guide/contributing) for more details.

---

**Built with ❤️ using [VitePress](https://vitepress.dev/)**
