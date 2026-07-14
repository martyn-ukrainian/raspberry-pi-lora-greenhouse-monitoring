module.exports = {
  content: ["./App.tsx", "./screens/**/*.{js,jsx,ts,tsx}", "./components/**/*.{js,jsx,ts,tsx}"],
  presets: [require("nativewind/preset")],
  theme: {
    extend: {
      colors: {
        brand: "#586E5A",
      }
    },
  },
  plugins: [],
};
